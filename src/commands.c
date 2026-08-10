#include "commands.h"

#include <stdio.h>
#include <string.h>

#include "file.h"
#include "protocol.h"
#include "ui.h"
#include "utils.h"

/**
 * @brief 读空 stdin 残留直到换行或 EOF
 *        用于 fgets 缓冲满但行未读完（超长行）时的清理，避免污染下一轮解析
 */
static void drain_stdin(void)
{
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
    }
}

/**
 * @brief 提示输入一行文本，超长则清理并返回 1 要求重输，EOF 返回 -1
 */
static int prompt_line(const char *label, char *buf, size_t buf_size)
{
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        if (fgets(buf, buf_size, stdin) == NULL) {
            return -1;  /* EOF（Ctrl-D） */
        }
        if (strchr(buf, '\n') == NULL) {
            /* 缓冲满但行未读完 → 超长 */
            drain_stdin();
            printf("输入过长（上限 %zu 字符），请重试\n", buf_size - 1);
            continue;
        }
        utils_trim_newline(buf);
        return 0;
    }
}

int commands_login(conn_t *conn)
{
    for (;;) {
        char user[MAX_USERNAME_LEN];
        char pass[MAX_PASSWORD_LEN];

        if (prompt_line("用户名: ", user, sizeof(user)) < 0) {
            return -1;
        }
        if (prompt_line("密码: ", pass, sizeof(pass)) < 0) {
            return -1;
        }
        if (user[0] == '\0' || pass[0] == '\0') {
            printf("用户名和密码不能为空，请重试\n");
            continue;
        }

        /* MSG_AUTH 载荷: username\0password */
        uint8_t frame[PROTO_HEADER_SIZE + MAX_USERNAME_LEN + MAX_PASSWORD_LEN];
        int n = protocol_build_text2(MSG_AUTH, user, pass, frame, sizeof(frame));
        if (n < 0) {
            return -1;
        }
        if (conn_send_all(conn, frame, n) < 0) {
            return -1;
        }
        return 0;  /* 发送成功；认证结果由 app 的登录循环等待 */
    }
}

int commands_parse_priv(const char *line, char *target, size_t target_sz,
                        const char **msg_out)
{
    const char *p = line + strlen("/priv");  /* 跳过命令前缀 */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺 target */
    }
    const char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    size_t tlen = (size_t)(p - start);
    if (tlen == 0 || tlen >= target_sz) {
        return -1;  /* target 为空或超长（需留 NUL 位） */
    }
    memcpy(target, start, tlen);
    target[tlen] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺消息 */
    }
    *msg_out = p;  /* 消息保留内部空格，直接指向行内 */
    return 0;
}

int commands_parse_sendfile(const char *line, char *target, size_t target_sz,
                            char *filename, size_t filename_sz)
{
    const char *p = line + strlen("/sendfile");  /* 跳过命令前缀 */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺目标 */
    }
    const char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    size_t tlen = (size_t)(p - start);
    if (tlen == 0 || tlen >= target_sz) {
        return -1;  /* 目标为空或超长（需留 NUL 位） */
    }
    memcpy(target, start, tlen);
    target[tlen] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺文件名 */
    }
    size_t flen = strlen(p);
    if (flen >= filename_sz) {
        return -1;  /* 文件名超长（需留 NUL 位） */
    }
    memcpy(filename, p, flen);  /* 文件名内部空格保留 */
    filename[flen] = '\0';
    return 0;
}

int commands_parse_tid_arg(const char *arg, uint32_t *tid_out)
{
    const char *p = arg;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 空参数 */
    }
    uint64_t tid;
    if (utils_parse_u64_range(p, p + strlen(p), &tid) < 0) {
        return -1;  /* 拒绝字母/负号/混合/溢出 */
    }
    if (tid > UINT32_MAX) {
        return -1;  /* 超 uint32（服务器 tid 为 4B 大端） */
    }
    *tid_out = (uint32_t)tid;
    return 0;
}

int commands_parse_gmsg(const char *line, char *group, size_t group_sz,
                        const char **msg_out)
{
    const char *p = line + strlen("/gmsg");  /* 跳过命令前缀 */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺群名 */
    }
    const char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    size_t glen = (size_t)(p - start);
    if (glen == 0 || glen >= group_sz) {
        return -1;  /* 群名为空或超长（需留 NUL 位） */
    }
    memcpy(group, start, glen);
    group[glen] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;  /* 缺消息 */
    }
    *msg_out = p;  /* 消息保留内部空格，直接指向行内 */
    return 0;
}

/**
 * @brief 单参命令通用处理：校验并发送单文本载荷帧（/gcreate /gjoin /gleave）
 * @param conn     已建立的连接
 * @param msg_type 消息类型（MSG_GCREATE / MSG_GJOIN / MSG_GLEAVE）
 * @param arg      命令后剩余部分（含可能的前导空白）
 * @param usage    参数为空时提示的用法文本
 * @return 0 已处理（含参数错误提示）；-1 发送失败
 */
static int cmd_send_single_name(conn_t *conn, uint8_t msg_type,
                                const char *arg, const char *usage)
{
    while (*arg == ' ' || *arg == '\t') {
        arg++;
    }
    if (*arg == '\0') {
        ui_print(usage);
        return 0;
    }
    if (strlen(arg) >= MAX_GROUP_NAME_LEN) {
        char msg[64];
        snprintf(msg, sizeof(msg), "群名过长（上限 %d 字符）",
                 MAX_GROUP_NAME_LEN - 1);
        ui_print(msg);
        return 0;
    }
    uint8_t frame[PROTO_HEADER_SIZE + MAX_GROUP_NAME_LEN];
    int n = protocol_build_text1(msg_type, arg, frame, sizeof(frame));
    if (n < 0) {
        return -1;
    }
    return conn_send_all(conn, frame, n);
}

int commands_handle_line(conn_t *conn, const char *line, bool *quit_out)
{
    *quit_out = false;

    if (line[0] == '\0') {
        return 0;  /* 空行不发送 */
    }

    if (line[0] == '/') {
        if (strcmp(line, "/quit") == 0) {
            *quit_out = true;
            return 0;
        }
        if (strcmp(line, "/help") == 0) {
            ui_print_help();
            return 0;
        }
        if (strcmp(line, "/login") == 0) {
            /* 已登录状态重新登录：发送新凭据，结果异步显示
             * （认证错误帧由主循环显示，不自动重试——重试仅限登录阶段） */
            return commands_login(conn);
        }
        if (strncmp(line, "/priv", 5) == 0 &&
            (line[5] == ' ' || line[5] == '\t' || line[5] == '\0')) {
            /* /priv <用户名> <消息> → MSG_PRIV 帧（target\0message） */
            char target[MAX_USERNAME_LEN];
            const char *msg = NULL;
            if (commands_parse_priv(line, target, sizeof(target), &msg) < 0) {
                ui_print("用法: /priv <用户名> <消息>");
                return 0;
            }
            uint8_t frame[PROTO_HEADER_SIZE + MAX_MESSAGE_LEN +
                          MAX_USERNAME_LEN + 1];
            int n = protocol_build_text2(MSG_PRIV, target, msg,
                                         frame, sizeof(frame));
            if (n < 0) {
                return -1;
            }
            return conn_send_all(conn, frame, n);
        }
        if (strcmp(line, "/users") == 0) {
            /* /users → 零载荷 MSG_USERS 请求，服务器回 MSG_ONLINE_USERS */
            uint8_t frame[PROTO_HEADER_SIZE];
            int n = protocol_build_frame(MSG_USERS, NULL, 0,
                                         frame, sizeof(frame));
            if (n < 0) {
                return -1;
            }
            return conn_send_all(conn, frame, n);
        }
        if (strncmp(line, "/gcreate", 8) == 0 &&
            (line[8] == ' ' || line[8] == '\t' || line[8] == '\0')) {
            /* /gcreate <群名> → MSG_GCREATE 帧（group_name） */
            return cmd_send_single_name(conn, MSG_GCREATE, line + 8,
                                        "用法: /gcreate <群名>");
        }
        if (strncmp(line, "/gjoin", 6) == 0 &&
            (line[6] == ' ' || line[6] == '\t' || line[6] == '\0')) {
            /* /gjoin <群名> → MSG_GJOIN 帧（group_name） */
            return cmd_send_single_name(conn, MSG_GJOIN, line + 6,
                                        "用法: /gjoin <群名>");
        }
        if (strncmp(line, "/gleave", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t' || line[7] == '\0')) {
            /* /gleave <群名> → MSG_GLEAVE 帧（group_name） */
            return cmd_send_single_name(conn, MSG_GLEAVE, line + 7,
                                        "用法: /gleave <群名>");
        }
        if (strncmp(line, "/gmsg", 5) == 0 &&
            (line[5] == ' ' || line[5] == '\t' || line[5] == '\0')) {
            /* /gmsg <群名> <消息> → MSG_GMSG 帧（group_name\0message） */
            char group[MAX_GROUP_NAME_LEN];
            const char *msg = NULL;
            if (commands_parse_gmsg(line, group, sizeof(group), &msg) < 0) {
                ui_print("用法: /gmsg <群名> <消息>");
                return 0;
            }
            uint8_t frame[PROTO_HEADER_SIZE + MAX_MESSAGE_LEN +
                          MAX_GROUP_NAME_LEN + 1];
            int n = protocol_build_text2(MSG_GMSG, group, msg,
                                         frame, sizeof(frame));
            if (n < 0) {
                return -1;
            }
            return conn_send_all(conn, frame, n);
        }
        if (strncmp(line, "/sendfile", 9) == 0 &&
            (line[9] == ' ' || line[9] == '\t' || line[9] == '\0')) {
            /* /sendfile <用户> <文件> → MSG_FILE_INIT（状态机在 file 模块） */
            return file_cmd_sendfile(conn, line);
        }
        if (strncmp(line, "/accept", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t' || line[7] == '\0')) {
            /* /accept <tid> → MSG_FILE_ACCEPT（接收侧状态机在 file 模块） */
            return file_cmd_accept(conn, line);
        }
        if (strncmp(line, "/reject", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t' || line[7] == '\0')) {
            /* /reject <tid> → MSG_FILE_REJECT（接收侧状态机在 file 模块） */
            return file_cmd_reject(conn, line);
        }
        if (strncmp(line, "/cancel", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t' || line[7] == '\0')) {
            /* /cancel <tid> → MSG_FILE_CANCEL（发送与接收任务均匹配） */
            return file_cmd_cancel(conn, line);
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "未知命令: %s（输入 /help 查看帮助）", line);
        ui_print(msg);
        return 0;
    }

    /* 裸文本 → 大厅聊天消息 */
    uint8_t frame[PROTO_HEADER_SIZE + MAX_MESSAGE_LEN + 1];
    int n = protocol_build_text1(MSG_CHAT, line, frame, sizeof(frame));
    if (n < 0) {
        return -1;
    }
    return conn_send_all(conn, frame, n);
}
