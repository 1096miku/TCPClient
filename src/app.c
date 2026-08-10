#include "app.h"

#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

#include "commands.h"
#include "file.h"
#include "protocol.h"
#include "ui.h"

/* main.c 定义，此处 extern 引用（镜像服务器 main.c/server.c 的关系） */
extern volatile sig_atomic_t g_running;

/* login_loop 中需要分发同批粘连帧，前向声明（实现见"帧分发"小节） */
static void app_dispatch(app_t *app);

/* ---------- 登录握手 ---------- */

/**
 * @brief 保证读缓冲有 recv 空余（容量满则翻倍扩容，超硬顶视为协议错误）
 * @return true 可用；false 致命错误（缓冲超限/内存不足）
 */
static bool app_ensure_capacity(app_t *app)
{
    /* recv 前必须保证缓冲有空余，否则 recv(..., 0) 立即返回 0 被误判为 EOF */
    if (app->rbuf_len < app->rbuf_cap) {
        return true;
    }
    size_t new_cap = app->rbuf_cap * 2;
    if (new_cap > APP_RBUF_MAX) {
        ui_print("读缓冲超限，协议错误，断开连接。");
        app->running = false;
        return false;
    }
    uint8_t *nb = realloc(app->rbuf, new_cap);
    if (nb == NULL) {
        ui_print("内存不足，退出。");
        app->running = false;
        return false;
    }
    app->rbuf = nb;
    app->rbuf_cap = new_cap;
    return true;
}

/**
 * @brief 登录循环：提示输入→发送 MSG_AUTH→等待服务器认证结果
 *
 * 服务器语义（auth.c）：登录失败只发错误帧、不关连接，
 * 因此收到认证类错误帧后在同一连接上重新提示输入（无限重试）。
 * 收到第一条 MSG_SERVER_MSG（Welcome 公告）即视为登录成功——
 * Welcome 是服务器认证成功的首个帧（auth.c:43-46），
 * 之后的离线消息/在线列表由主循环接管处理。
 *
 * 重要：登录期间直接用 app->rbuf 做读缓冲——Welcome 帧与在线列表帧
 * 可能在同一批 recv 数据中粘连到达，未解析的剩余数据必须留在 rbuf
 * 里交给主循环（否则在线列表帧会被丢弃）。
 */
static int login_loop(app_t *app)
{
    conn_t *conn = app->conn;

    for (;;) {
        if (commands_login(conn) < 0) {
            return -1;  /* 输入 EOF（Ctrl-D）或发送失败 */
        }

        /* 等待服务器认证结果 */
        for (;;) {
            struct pollfd pfd;
            pfd.fd = conn_get_fd(conn);
            pfd.events = POLLIN;
            pfd.revents = 0;

            int r = poll(&pfd, 1, APP_POLL_TIMEOUT_MS);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (!g_running) {
                return -1;  /* Ctrl-C 已按下，放弃登录 */
            }
            if (r == 0) {
                continue;  /* 超时，继续等待 */
            }

            if (!app_ensure_capacity(app)) {
                return -1;
            }
            ssize_t n = recv(conn_get_fd(conn),
                             app->rbuf + app->rbuf_len,
                             app->rbuf_cap - app->rbuf_len, 0);
            if (n <= 0) {
                ui_print("服务器已断开连接。");
                return -1;
            }
            app->rbuf_len += (size_t)n;

            /* 增量解析本批数据 */
            size_t off = 0;
            int outcome = 0;  /* 0=继续等待 1=登录成功 2=认证失败重试 */
            while (off < app->rbuf_len) {
                uint8_t type;
                const uint8_t *payload;
                uint16_t plen;
                int consumed = protocol_parse(app->rbuf + off,
                                              (int)(app->rbuf_len - off),
                                              &type, &payload, &plen);
                if (consumed < 0) {
                    ui_print("协议错误（magic 不符），断开连接。");
                    return -1;
                }
                if (consumed == 0) {
                    break;  /* 半帧，等待更多数据 */
                }
                off += (size_t)consumed;

                if (type == MSG_SERVER_MSG) {
                    ui_display_incoming((const char *)payload);  /* Welcome */
                    outcome = 1;
                    break;
                }
                if (type == MSG_ERROR) {
                    /* 认证失败：显示错误后重试（同一连接） */
                    ui_print_error(utils_read_u16_be(payload),
                                   (const char *)payload + 3);
                    outcome = 2;
                    break;
                }
                /* 其他类型登录期间不应出现，忽略 */
            }
            /* 未解析的剩余数据（如粘连的在线列表帧）留在 rbuf 交给主循环 */
            memmove(app->rbuf, app->rbuf + off, app->rbuf_len - off);
            app->rbuf_len -= off;

            if (outcome == 1) {
                /* Welcome 与后续帧（离线公告/回放/在线列表/上线公告）可能
                 * 同批到达——数据已从 socket 读入 rbuf，主循环 poll 只监听
                 * socket fd，无新数据时永远不会再触发分发，必须在此立即
                 * 分发剩余帧（否则离线回放/在线列表被吞） */
                app_dispatch(app);
                return 0;
            }
            if (outcome == 2) {
                break;  /* 重新提示输入凭据 */
            }
        }
    }
}

/* ---------- 帧分发 ---------- */

/**
 * @brief 帧分发：按类型路由到对应处理
 *
 * M4 的新帧类型（MSG_FILE_* 帧族）在此 switch 上扩展。
 * 默认分支提示但不终止循环。
 */
static void app_handle_frame(app_t *app, uint8_t type,
                             const uint8_t *payload, uint16_t plen)
{
    switch (type) {
    case MSG_SERVER_MSG:
        /* 服务器公告已格式化（[Server] 前缀），原样打印；
         * 文件传输公告另驱动状态迁移（公告为主信号，帧为冗余） */
        ui_display_incoming((const char *)payload);
        file_handle_announcement(app->conn, (const char *)payload);
        break;
    case MSG_CHAT:
        /* 服务器已格式化 "发送者: 消息"——含自己的回显，预期行为 */
        ui_display_incoming((const char *)payload);
        break;
    case MSG_PRIV:
        /* 服务器已格式化三种形态（chat.c:38-63 / auth.c:64-70）：
         * 目标收 "发送者 (private): 消息"、发送方收 "To 目标: 消息" 回声、
         * 离线回放 "发送者 (offline): 消息"，全部原样打印 */
        ui_display_incoming((const char *)payload);
        break;
    case MSG_GMSG:
        /* 群聊广播 "[群名] 发送者: 消息"（chat.c:92-104）——
         * 含发送者本人回显，同大厅广播的预期行为，原样打印 */
        ui_display_incoming((const char *)payload);
        break;
    case MSG_ERROR:
        /* 载荷: code(2B 大端)\0message\0；错误挂钩清理匹配的传输任务 */
        if (plen >= 3) {
            uint16_t code = utils_read_u16_be(payload);
            ui_print_error(code, (const char *)payload + 3);
            file_handle_error(code);
        }
        break;
    case MSG_ONLINE_USERS:
        ui_display_online_users(payload, plen);
        break;
    case MSG_FILE_INIT:
    case MSG_FILE_ACCEPT:
    case MSG_FILE_REJECT:
    case MSG_FILE_CHUNK:
    case MSG_FILE_COMPLETE:
    case MSG_FILE_CANCEL:
        /* 文件传输帧族：发送方视角（ACCEPT/REJECT/CANCEL）与
         * 接收方视角（INIT/CHUNK/COMPLETE/CANCEL）统一进 file 模块 */
        file_handle_frame(app->conn, type, payload, plen);
        break;
    default:
    {
        /* 未知类型的防御提示（M5 扩展点） */
        char msg[128];
        snprintf(msg, sizeof(msg), "收到未处理的消息类型 0x%02X", type);
        ui_print(msg);
        break;
    }
    }
}

/**
 * @brief 增量解析读缓冲中的全部完整帧并分发
 *
 * 半帧留在缓冲中等待更多数据；magic 错误是致命错误（返回 -1 语义：
 * 丢弃整个缓冲并断开，不尝试重新同步）。
 */
static void app_dispatch(app_t *app)
{
    size_t off = 0;
    while (off < app->rbuf_len) {
        uint8_t type;
        const uint8_t *payload;
        uint16_t plen;
        int consumed = protocol_parse(app->rbuf + off,
                                      (int)(app->rbuf_len - off),
                                      &type, &payload, &plen);
        if (consumed == 0) {
            break;  /* 半帧，等待更多数据 */
        }
        if (consumed < 0) {
            ui_print("协议错误（magic 不符），断开连接。");
            app->running = false;
            return;
        }
        app_handle_frame(app, type, payload, plen);
        off += (size_t)consumed;
    }
    /* 已消费的字节归位，剩余半帧数据移到缓冲头部 */
    memmove(app->rbuf, app->rbuf + off, app->rbuf_len - off);
    app->rbuf_len -= off;
}

/* ---------- poll 事件循环的读写处理 ---------- */

/**
 * @brief 处理 socket 可读：扩容→recv→增量解析分发
 * @return true 继续循环；false 应退出（EOF/错误/协议错误）
 */
static bool app_handle_socket(app_t *app)
{
    if (!app_ensure_capacity(app)) {
        return false;
    }

    ssize_t n = recv(conn_get_fd(app->conn),
                     app->rbuf + app->rbuf_len,
                     app->rbuf_cap - app->rbuf_len, 0);
    if (n == 0) {
        ui_print("服务器已断开连接。");
        app->running = false;
        return false;
    }
    if (n < 0) {
        if (errno == EINTR) {
            return true;
        }
        ui_print("接收失败，退出。");
        app->running = false;
        return false;
    }
    app->rbuf_len += (size_t)n;
    app_dispatch(app);
    return true;
}

/**
 * @brief 处理 stdin 可读：读一行→命令解析
 * @return true 继续循环；false 应退出（EOF/发送失败/quit）
 */
static bool app_handle_stdin(app_t *app)
{
    char line[APP_INPUT_MAX];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        /* stdin EOF（Ctrl-D） */
        ui_print("再见。");
        app->running = false;
        return false;
    }

    if (strchr(line, '\n') == NULL) {
        /* 行尾无换行 → 超长行：读空残留再丢弃，避免污染下一轮解析 */
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {
        }
        ui_print("输入过长，已丢弃（上限 65535 字节）。");
        return true;
    }

    utils_trim_newline(line);

    bool quit = false;
    if (commands_handle_line(app->conn, line, &quit) < 0) {
        app->running = false;  /* 发送失败（连接已断） */
        return false;
    }
    if (quit) {
        app->running = false;
        return false;
    }

    ui_prompt();  /* 一行处理完，重打提示符 */
    return true;
}

/* ---------- 对外接口 ---------- */

app_t *app_connect(const char *host, uint16_t port)
{
    char errbuf[256];
    conn_t *conn = conn_connect(host, port, errbuf, sizeof(errbuf));
    if (conn == NULL) {
        fprintf(stderr, "连接失败(%s:%u): %s\n", host, port, errbuf);
        return NULL;
    }

    app_t *app = calloc(1, sizeof(app_t));
    if (app == NULL) {
        conn_close(conn);
        return NULL;
    }
    app->conn = conn;
    app->rbuf = malloc(APP_RBUF_INIT);
    if (app->rbuf == NULL) {
        conn_close(conn);
        free(app);
        return NULL;
    }
    app->rbuf_cap = APP_RBUF_INIT;
    app->rbuf_len = 0;
    app->running = true;

    /* 登录握手在 app 创建后进行（登录期间 Welcome 帧与在线列表帧
     * 可能粘连到达，剩余数据留在 app->rbuf 交给主循环） */
    if (login_loop(app) < 0) {
        app_destroy(app);
        return NULL;
    }
    file_reset_all();  /* 干净起点（防御：文件传输任务不应残留到新会话） */
    return app;
}

int app_run(app_t *app)
{
    while (app->running && g_running) {
        struct pollfd fds[2];
        fds[0].fd = conn_get_fd(app->conn);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = STDIN_FILENO;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        /* 文件发送期间缩短 poll 超时，让分片批次推进更平滑（ADR-0002） */
        int timeout = file_send_active() ? FILE_SEND_POLL_TIMEOUT_MS
                                         : APP_POLL_TIMEOUT_MS;
        int r = poll(fds, 2, timeout);
        if (r < 0) {
            if (errno == EINTR) {
                continue;  /* 信号打断：下一轮循环检查 g_running */
            }
            ui_print("poll 失败，退出。");
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (!app_handle_socket(app)) {
                break;
            }
        }
        if (fds[1].revents & POLLIN) {
            if (!app_handle_stdin(app)) {
                break;
            }
        }
        /* 发送分片：只在 poll 超时轮执行（r==0）。stdin 先处理，
         * 保证当轮 /cancel 立即生效不残留。
         *
         * 节流原理：轮询分片的"每轮 50ms"依赖 poll 超时——但转发数据
         * 连续到达时（自环传输实测：接收流即自己发送的转发），poll 每轮
         * 立即返回，循环塌缩成 ~1ms 忙循环，发送速率失控
         * （4MB 全部片在 47ms 内发出），服务器 worker 转发积压超写缓冲
         * 上限静默丢包（strace 实测，2026-08-10）。事件轮跳过 tick，
         * 数据消化完后自然回到超时轮，发送节奏恒定为 2 片/50ms */
        if (r == 0 && file_send_active()) {
            if (file_send_tick(app->conn) < 0) {
                break;  /* 发送失败 = 连接已断 */
            }
        }
    }
    return 0;
}

void app_destroy(app_t *app)
{
    if (app == NULL) {
        return;
    }
    file_reset_all();       /* 关文件句柄、清传输任务（断线取消由服务器公告对端） */
    conn_close(app->conn);  /* 关闭连接 → 服务器广播 "X has left the chat" */
    free(app->rbuf);
    free(app);
}
