#include "file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "commands.h"
#include "protocol.h"
#include "ui.h"
#include "utils.h"

/**
 * @brief 解析服务器公告文本：分类文件传输事件并提取传输标识
 *
 * 服务器把文件传输的 tid 只放在公告文本里（协议事实，见 CONTEXT.md「传输标识」），
 * 控制帧载荷中只有二进制 tid。公告是主驱动信号，控制帧是冗余信号（幂等双信号）。
 *
 * 识别规则：剥 "[Server] " 前缀后，文本必须以
 * "File transfer #" 或 "Incoming file transfer #" 开头（防止聊天文本误匹配），
 * tid 数字后须为 ':' 或 ' '，且 ≤ UINT32_MAX（服务器全局单调递增）。
 */
int file_parse_announcement(const char *text, file_ann_kind_t *kind_out,
                            uint32_t *tid_out)
{
    const char *p = text;
    if (strncmp(p, "[Server] ", 9) == 0) {
        p += 9;
    }

    /* 匹配开头并定位 tid 起始 */
    const char *num = NULL;
    if (strncmp(p, "Incoming file transfer #", 24) == 0) {
        num = p + 24;
        *kind_out = FILE_ANN_RECV_PENDING;
    } else if (strncmp(p, "File transfer #", 15) == 0) {
        num = p + 15;
        *kind_out = FILE_ANN_SEND_PENDING;  /* 暂定，下面细分 */
    } else {
        *kind_out = FILE_ANN_NONE;
        return -1;
    }

    /* 提取 tid：数字后须为 ':' 或 ' ' */
    const char *dig = num;
    while (*dig >= '0' && *dig <= '9') {
        dig++;
    }
    if (dig == num || (*dig != ':' && *dig != ' ')) {
        *kind_out = FILE_ANN_NONE;
        return -1;
    }

    uint64_t tid;
    if (utils_parse_u64_range(num, dig, &tid) < 0 || tid > UINT32_MAX) {
        *kind_out = FILE_ANN_NONE;
        return -1;  /* 溢出（服务器 tid 为 4B 大端） */
    }

    /* 细分类型（仅 "File transfer #" 前缀需要；Incoming 即 RECV_PENDING） */
    if (*kind_out != FILE_ANN_RECV_PENDING) {
        if (strstr(p, "Waiting for acceptance")) {
            *kind_out = FILE_ANN_SEND_PENDING;
        } else if (strstr(p, "accepted by")) {
            *kind_out = FILE_ANN_ACCEPTED;
        } else if (strstr(p, "rejected by")) {
            *kind_out = FILE_ANN_REJECTED;
        } else if (strstr(p, "complete")) {
            *kind_out = FILE_ANN_COMPLETE;
        } else if (strstr(p, "cancelled by")) {
            *kind_out = FILE_ANN_CANCELLED;
        } else if (strstr(p, "cancelled: ")) {
            *kind_out = FILE_ANN_CANCELLED_DISC;
        } else {
            *kind_out = FILE_ANN_NONE;
            return -1;  /* 无法识别的文件公告形态 */
        }
    }

    *tid_out = (uint32_t)tid;
    return 0;
}

/**
 * @brief 解析 INIT 帧载荷：sender\0filename\0size_str（三段 NUL 结尾）
 *
 * 服务器转发给接收方的 INIT 帧载荷首段是发送方用户名（file_transfer.c
 * payload_append_field 语义）。size 用全数字逐位解析（拒绝空白/符号/混合，
 * 与服务器 strtoll 的宽容语义相比更严格——客户端自己发合法数字）。
 */
int file_parse_init_payload(const uint8_t *payload, uint16_t plen,
                            char *sender, size_t sender_sz,
                            char *filename, size_t filename_sz,
                            uint64_t *size_out)
{
    const char *p = (const char *)payload;
    const char *end = p + plen;

    /* 段 1：sender */
    const char *s1 = p;
    p = memchr(p, '\0', (size_t)(end - p));
    if (p == NULL) {
        return -1;
    }
    size_t l1 = (size_t)(p - s1);
    p++;

    /* 段 2：filename */
    if (p >= end) {
        return -1;
    }
    const char *s2 = p;
    p = memchr(p, '\0', (size_t)(end - p));
    if (p == NULL) {
        return -1;
    }
    size_t l2 = (size_t)(p - s2);
    p++;

    /* 段 3：size_str */
    if (p >= end) {
        return -1;
    }
    const char *s3 = p;
    p = memchr(p, '\0', (size_t)(end - p));
    if (p == NULL) {
        return -1;
    }
    size_t l3 = (size_t)(p - s3);

    if (l1 == 0 || l1 >= sender_sz) {
        return -1;
    }
    if (l2 == 0 || l2 >= filename_sz) {
        return -1;
    }
    if (l3 == 0) {
        return -1;
    }

    uint64_t size;
    if (utils_parse_u64_range(s3, s3 + l3, &size) < 0) {
        return -1;  /* 非数字（含空白/符号/混合）或溢出 */
    }

    memcpy(sender, s1, l1);
    sender[l1] = '\0';
    memcpy(filename, s2, l2);
    filename[l2] = '\0';
    *size_out = size;
    return 0;
}

/**
 * @brief 文件名 basename 化 + 控制字符清理（防目录穿越）
 *
 * 服务器不校验文件名内容（只查长度 < 256），"../evil" 是合法输入——
 * 客户端必须自行清理（CONTEXT.md「接收目录」行为规则）。
 */
int file_basename_sanitize(const char *raw, char *out, size_t out_sz)
{
    /* 定位最后一个 '/' 或 '\\' 之后 */
    const char *base = raw;
    for (const char *p = raw; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    size_t len = strlen(base);
    if (len == 0 || len >= out_sz) {
        return -1;
    }
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        char c = base[i];
        out[i] = (c < 0x20) ? '_' : c;
    }
    out[len] = '\0';
    return 0;
}

bool file_path_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

/**
 * @brief 同名唯一化：base、base (1)、base (2)... 首个不冲突者
 *
 * exists 探针由调用方注入：生产传文件系统探针（拼接 downloads/ 路径后探测），
 * 测试传内存假探针——同一函数两种用法，独立可测。
 */
int file_unique_name(const char *base,
                     bool (*exists)(const char *name, void *ctx), void *ctx,
                     char *out, size_t out_sz)
{
    char candidate[512];  /* base ≤ 255 + " (999)" 后缀，留足余量 */
    for (unsigned i = 0; i < 1000; i++) {
        int n;
        if (i == 0) {
            n = snprintf(candidate, sizeof(candidate), "%s", base);
        } else {
            n = snprintf(candidate, sizeof(candidate), "%s (%u)", base, i);
        }
        if (n < 0 || (size_t)n >= sizeof(candidate)) {
            return -1;
        }
        if (!exists(candidate, ctx)) {
            if (strlen(candidate) >= out_sz) {
                return -1;
            }
            strcpy(out, candidate);
            return 0;
        }
    }
    return -1;  /* 试满 1000 次仍冲突（实践中不会发生） */
}

int file_join_path(const char *dir, const char *name, char *out, size_t out_sz)
{
    int n = snprintf(out, out_sz, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= out_sz) {
        return -1;
    }
    return 0;
}

/**
 * @brief 分片总数：size=0 → 0；否则 ceil(size / chunk_size)
 *
 * 用除余写法而非 (size + chunk_size - 1) / chunk_size，避免大 size 时加法溢出。
 */
uint64_t file_chunk_count(uint64_t size, uint32_t chunk_size)
{
    if (size == 0) {
        return 0;
    }
    return size / chunk_size + (size % chunk_size != 0 ? 1 : 0);
}

bool file_chunk_plan(uint64_t size, uint32_t chunk_size, uint64_t index,
                     uint64_t *offset_out, uint32_t *len_out)
{
    uint64_t count = file_chunk_count(size, chunk_size);
    if (index >= count) {
        return false;
    }
    uint64_t offset = index * chunk_size;
    uint64_t remain = size - offset;
    *offset_out = offset;
    *len_out = (remain >= chunk_size) ? chunk_size : (uint32_t)remain;
    return true;
}

/**
 * @brief 接收分片合法性校验：片对齐、不越界、不重复
 *
 * bitmap 按片号去重（bit i = 第 i 片，片号 = offset / FILE_CHUNK_DATA_MAX）。
 * 发送方固定以 FILE_CHUNK_DATA_MAX 对齐分片（file_chunk_plan），服务器
 * 乱序转发是正常现象（spec Further Notes）——乱序片合法；重复/越界才错。
 * 非对齐 offset 只可能来自异常发送方：若接收会污染 bitmap（同一片号的
 * 不同偏移无法区分，误伤后续合法片），故一并判错。
 */
/* 片号定位：返回片号所在 bit 的字节下标；-1 非对齐或 bitmap 长度不足 */
static long recv_bitmap_index(size_t bitmap_len, uint64_t offset)
{
    if (offset % FILE_CHUNK_DATA_MAX != 0) {
        return -1;
    }
    uint64_t chunk = offset / FILE_CHUNK_DATA_MAX;
    if (chunk / 8 >= bitmap_len) {
        return -1;  /* bitmap 长度不足（防御，/accept 时按 size 分配） */
    }
    return (long)(chunk / 8);
}

int file_recv_span_check(uint64_t size, const uint8_t *bitmap, size_t bitmap_len,
                         uint64_t offset, uint32_t len)
{
    if (size == 0 || len == 0 || offset >= size) {
        return -1;
    }
    if ((uint64_t)len > size - offset || len > FILE_CHUNK_DATA_MAX) {
        return -1;  /* 越界或异常超大片（发送方单片 ≤ 65500） */
    }
    long idx = recv_bitmap_index(bitmap_len, offset);
    if (idx < 0) {
        return -1;
    }
    uint64_t chunk = offset / FILE_CHUNK_DATA_MAX;
    return (bitmap[idx] & (uint8_t)(1u << (chunk % 8))) ? -1 : 0;  /* 重复 */
}

int file_recv_span_mark(uint8_t *bitmap, size_t bitmap_len, uint64_t offset)
{
    long idx = recv_bitmap_index(bitmap_len, offset);
    if (idx < 0) {
        return -1;
    }
    uint64_t chunk = offset / FILE_CHUNK_DATA_MAX;
    bitmap[idx] |= (uint8_t)(1u << (chunk % 8));
    return 0;
}

/* ==================== 发送状态机 ==================== */

/* 接收任务辅助（定义见文件末尾"接收状态机"小节）：
 * 发送侧函数（file_cmd_cancel / announcement / frame / error / reset）
 * 需要处理接收任务，先声明 */
static file_recv_task_t *recv_task_find(uint32_t tid);
static file_recv_task_t *recv_task_active(void);
static void recv_task_clear(file_recv_task_t *t);
static void recv_task_finish(file_recv_task_t *t);
static void recv_task_complete(uint32_t tid);
static void recv_announce_incoming(conn_t *conn, uint32_t tid);
static void recv_handle_init(const uint8_t *payload, uint16_t plen);
static void recv_handle_chunk(conn_t *conn, const uint8_t *payload, uint16_t plen);

/* 发送任务：全局单槽（ADR-0002：单槽限并发），模块级状态 */
static file_send_task_t g_send;

/* 接收任务数组：≤ FILE_MAX_RECV_PENDING 个 pending + 至多 1 个活动接收
 * （ADR-0002 单槽规则；活动接收 = fp 非空）。数组与辅助函数实现见
 * 文件末尾"接收状态机"小节（发送侧函数也需访问） */
static file_recv_task_t g_recv[FILE_MAX_RECV_PENDING];
static uint64_t g_recv_seq = 0;  /* 公告到达序号（INIT 帧 FIFO 关联依据） */

/* 清理发送任务：关文件句柄、清零（服务器侧 transfer 由取消/断线语义接管） */
static void send_task_clear(void)
{
    if (g_send.fp != NULL) {
        fclose(g_send.fp);
        g_send.fp = NULL;
    }
    memset(&g_send, 0, sizeof(g_send));
}

static bool send_task_matches(uint32_t tid)
{
    return g_send.busy && g_send.tid == tid;
}

int file_cmd_sendfile(conn_t *conn, const char *line)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    if (commands_parse_sendfile(line, target, sizeof(target),
                                filename, sizeof(filename)) < 0) {
        ui_print("用法: /sendfile <用户> <文件>");
        return 0;
    }
    if (g_send.busy) {
        ui_print("已有进行中的发送任务，完成或取消后再试。");
        return 0;
    }

    /* 本地文件校验：可打开、大小可获取、非空 */
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        char msg[MAX_FILENAME_LEN + 64];
        snprintf(msg, sizeof(msg), "无法打开文件: %s", filename);
        ui_print(msg);
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        ui_print("无法读取文件。");
        return 0;
    }
    long fsize = ftell(fp);
    if (fsize < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        ui_print("无法读取文件。");
        return 0;
    }
    uint64_t size = (uint64_t)fsize;
    if (size == 0) {
        fclose(fp);
        ui_print("空文件无法发送。");
        return 0;
    }

    /* 登记任务：PENDING_TID（tid 待自己的公告解析，此刻尚不知道） */
    memset(&g_send, 0, sizeof(g_send));
    g_send.busy = true;
    g_send.state = FILE_SEND_PENDING_TID;
    g_send.fp = fp;
    g_send.size = size;
    g_send.next_progress_mark = FILE_PROGRESS_INTERVAL;
    strcpy(g_send.target, target);
    strcpy(g_send.filename, filename);

    /* MSG_FILE_INIT: target\0filename\0size_str（size 十进制字符串） */
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%llu", (unsigned long long)size);
    uint8_t frame[PROTO_HEADER_SIZE + MAX_USERNAME_LEN + MAX_FILENAME_LEN + 32];
    int n = protocol_build_text3(MSG_FILE_INIT, target, filename, size_str,
                                 frame, sizeof(frame));
    if (n < 0) {
        send_task_clear();
        return -1;
    }
    if (conn_send_all(conn, frame, n) < 0) {
        send_task_clear();
        return -1;
    }

    char msg[MAX_FILENAME_LEN + 96];
    snprintf(msg, sizeof(msg), "已提交文件传输: '%s' (%llu 字节) → %s",
             filename, (unsigned long long)size, target);
    ui_print(msg);
    return 0;
}

int file_cmd_cancel(conn_t *conn, const char *line)
{
    const char *arg = line + strlen("/cancel");
    uint32_t tid;
    if (commands_parse_tid_arg(arg, &tid) < 0) {
        ui_print("用法: /cancel <tid>");
        return 0;
    }
    if (send_task_matches(tid)) {
        /* 服务器只通知对端取消，取消方自己收不到反馈——本地清理 */
        uint8_t frame[PROTO_HEADER_SIZE + 4];
        int n = protocol_build_tid(MSG_FILE_CANCEL, tid, frame, sizeof(frame));
        if (n < 0) {
            return -1;
        }
        if (conn_send_all(conn, frame, n) < 0) {
            return -1;
        }
        send_task_clear();
        char msg[64];
        snprintf(msg, sizeof(msg), "已取消 #%u。", tid);
        ui_print(msg);
        return 0;
    }
    /* 接收任务（票 03）：发取消帧 + 删半成品 + 清理 */
    file_recv_task_t *rt = recv_task_find(tid);
    if (rt != NULL) {
        uint8_t frame[PROTO_HEADER_SIZE + 4];
        int n = protocol_build_tid(MSG_FILE_CANCEL, tid, frame, sizeof(frame));
        if (n < 0) {
            return -1;
        }
        if (conn_send_all(conn, frame, n) < 0) {
            return -1;
        }
        recv_task_clear(rt);
        char msg[64];
        snprintf(msg, sizeof(msg), "已取消 #%u。", tid);
        ui_print(msg);
        return 0;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "没有 #%u 的传输记录。", tid);
    ui_print(msg);
    return 0;
}

void file_handle_announcement(conn_t *conn, const char *text)
{
    file_ann_kind_t kind;
    uint32_t tid;
    if (file_parse_announcement(text, &kind, &tid) < 0) {
        return;  /* 非文件传输公告 */
    }
    switch (kind) {
    case FILE_ANN_SEND_PENDING:
        /* 自己的 INIT 公告：PENDING_TID → PENDING（tid 只存在于公告文本） */
        if (g_send.busy && g_send.state == FILE_SEND_PENDING_TID) {
            g_send.tid = tid;
            g_send.state = FILE_SEND_PENDING;
        }
        break;
    case FILE_ANN_ACCEPTED:
        if (send_task_matches(tid) && g_send.state == FILE_SEND_PENDING) {
            g_send.state = FILE_SEND_ACTIVE;  /* 轮询发片由 file_send_tick 驱动 */
        }
        break;
    case FILE_ANN_REJECTED:
        /* 接收侧不会收到 rejected 公告（REJECT 帧只发发送方），
         * 但发送侧会——两分支都试，幂等 */
        if (send_task_matches(tid)) {
            send_task_clear();
        }
        break;
    case FILE_ANN_CANCELLED:
    case FILE_ANN_CANCELLED_DISC:
        /* 取消公告：发送侧清理发送任务；接收侧删半成品（帧已先到则幂等） */
        if (send_task_matches(tid)) {
            send_task_clear();
        } else {
            file_recv_task_t *t = recv_task_find(tid);
            if (t != NULL) {
                recv_task_clear(t);
            }
        }
        break;
    case FILE_ANN_COMPLETE:
        /* complete 公告：发送侧完成确认；接收侧字节数校验（帧先到则幂等）。
         * 自环场景（发送方=接收方）下公告与 COMPLETE 帧先后到达，先到者
         * 完成收尾，后到者匹配不到任务——双信号幂等 */
        if (send_task_matches(tid) && g_send.state == FILE_SEND_WAIT_COMPLETE) {
            send_task_clear();  /* 槽位释放 */
        } else {
            recv_task_complete(tid);
        }
        break;
    case FILE_ANN_RECV_PENDING:
        /* 接收侧：incoming 公告建 pending 任务（tid 只存在于公告文本） */
        recv_announce_incoming(conn, tid);
        break;
    default:
        break;  /* FILE_ANN_NONE（解析失败已提前返回） */
    }
}

void file_handle_frame(conn_t *conn, uint8_t type,
                       const uint8_t *payload, uint16_t plen)
{
    if (plen == 4) {
        /* 4B tid 控制帧（ACCEPT/REJECT/CANCEL/COMPLETE），每个 case 直接返回 */
        uint32_t tid = utils_read_u32_be(payload);
        switch (type) {
        case MSG_FILE_ACCEPT:
            /* 发送方视角：已接受（公告先到，此分支常被跳过） */
            if (send_task_matches(tid) && g_send.state == FILE_SEND_PENDING) {
                g_send.state = FILE_SEND_ACTIVE;
            }
            return;
        case MSG_FILE_REJECT:
            /* 发送方视角：被拒绝（公告先到，幂等） */
            if (send_task_matches(tid)) {
                send_task_clear();
            }
            return;
        case MSG_FILE_CANCEL:
            /* 双方视角：对方取消（公告先到，幂等） */
            if (send_task_matches(tid)) {
                send_task_clear();
            } else {
                file_recv_task_t *t = recv_task_find(tid);
                if (t != NULL) {
                    recv_task_clear(t);  /* 删半成品 */
                }
            }
            return;
        case MSG_FILE_COMPLETE:
            /* 接收方视角：完成信号（complete 公告先到，幂等） */
            recv_task_complete(tid);
            return;
        default:
            break;  /* 未知 4B 控制帧，忽略 */
        }
        return;  /* 4B 帧不可能是 INIT/CHUNK */
    }
    switch (type) {
    case MSG_FILE_INIT:
        recv_handle_init(payload, plen);
        break;
    case MSG_FILE_CHUNK:
        recv_handle_chunk(conn, payload, plen);
        break;
    default:
        break;  /* 其余（异常帧/未知类型）忽略 */
    }
}

void file_handle_error(uint16_t code)
{
    if (code == ERR_NO_SUCH_USER) {
        /* 200 也可能来自 /priv 等无关命令——仅在 INIT 未确认阶段清理 */
        if (g_send.busy && g_send.state == FILE_SEND_PENDING_TID) {
            send_task_clear();
        }
    } else if (code == ERR_TRANSFER_STATE) {
        /* 300 只来自文件传输操作（错误文本 "Invalid transfer state"）。
         * 发送侧：任何发送状态均清理；接收侧：清理活动接收任务
         * （如 /accept 一个已被取消的传输，服务器 ft 已删 → 300）。
         * 错误帧无 tid 信息，无法按任务精确匹配——清理活动接收任务是
         * 启发式：发送与接收槽独立（ADR-0002），若 300 来自发送侧操作
         * 会误杀健康的活动接收任务（删半成品）。接受该局限（协议错误帧
         * 不带 tid 是无解约束）；pending 任务不碰（可能无辜） */
        if (g_send.busy) {
            send_task_clear();
        }
        file_recv_task_t *t = recv_task_active();
        if (t != NULL) {
            recv_task_clear(t);
        }
    }
}

bool file_send_active(void)
{
    return g_send.busy && (g_send.state == FILE_SEND_ACTIVE ||
                           g_send.state == FILE_SEND_DONE);
}

/**
 * @brief 轮询发送：每轮 ≤FILE_CHUNKS_PER_ROUND 片（≈128KB，见常量注释——
 *        服务器写缓冲 256KB 超限静默丢包，4 片/轮实测会触发，2 片留余量）
 *
 * 用 fread 的游标位置作为下一片 offset（无需 fseek）：
 * 发送前 fopen 后游标已在 0，逐片 fread 自然推进。
 *
 * 竞态规避：全部片发完后先置 FILE_SEND_DONE，**下一轮 tick 才发 COMPLETE 帧**。
 * 服务器线程池并行处理同连接帧，若 COMPLETE 先于末片 chunk 被处理
 * （状态提前置 FT_COMPLETE），末片会被拒绝丢弃（实测 3144000/3145728，
 * 见 spec Further Notes）——隔一个 poll 周期（≥50ms）让服务器先消化末片。
 */
int file_send_tick(conn_t *conn)
{
    if (!g_send.busy) {
        return 0;
    }
    if (g_send.state != FILE_SEND_ACTIVE && g_send.state != FILE_SEND_DONE) {
        return 0;
    }
    if (g_send.state == FILE_SEND_DONE) {
        /* 末片已发完一整个 poll 周期，此时发 COMPLETE 帧 */
        uint8_t frame[PROTO_HEADER_SIZE + 4];
        int n = protocol_build_tid(MSG_FILE_COMPLETE, g_send.tid,
                                   frame, sizeof(frame));
        if (n < 0) {
            return -1;
        }
        if (conn_send_all(conn, frame, n) < 0) {
            return -1;
        }
        g_send.state = FILE_SEND_WAIT_COMPLETE;
        return 0;
    }
    for (int i = 0; i < FILE_CHUNKS_PER_ROUND && g_send.sent < g_send.size; i++) {
        uint64_t offset = 0;
        uint32_t len = 0;
        if (!file_chunk_plan(g_send.size, FILE_CHUNK_DATA_MAX,
                             g_send.sent / FILE_CHUNK_DATA_MAX,
                             &offset, &len)) {
            send_task_clear();  /* 防御：sent < size 保证 index 合法，理论上不可达 */
            return 0;
        }

        uint8_t data[FILE_CHUNK_DATA_MAX];
        if (fread(data, 1, len, g_send.fp) != len) {
            /* 磁盘错误/文件被改短：主动取消并清理 */
            ui_print("读取文件失败，已取消传输。");
            uint8_t frame[PROTO_HEADER_SIZE + 4];
            int n = protocol_build_tid(MSG_FILE_CANCEL, g_send.tid,
                                       frame, sizeof(frame));
            if (n >= 0) {
                (void)conn_send_all(conn, frame, n);
            }
            send_task_clear();
            return 0;
        }

        uint8_t frame[PROTO_HEADER_SIZE + 12 + FILE_CHUNK_DATA_MAX];
        int n = protocol_build_chunk(g_send.tid, offset, data, len,
                                     frame, sizeof(frame));
        if (n < 0) {
            send_task_clear();
            return -1;
        }
        if (conn_send_all(conn, frame, n) < 0) {
            send_task_clear();
            return -1;
        }
        g_send.sent += len;

        /* 进度：每越过 1 MiB 水位打一行（异步事件走换行保护） */
        if (g_send.sent >= g_send.next_progress_mark) {
            char msg[160];
            unsigned pct = (unsigned)(g_send.sent * 100 / g_send.size);
            snprintf(msg, sizeof(msg), "发送中 #%u: %llu/%llu 字节 (%u%%)",
                     g_send.tid,
                     (unsigned long long)g_send.sent,
                     (unsigned long long)g_send.size, pct);
            ui_display_incoming(msg);
            g_send.next_progress_mark += FILE_PROGRESS_INTERVAL;
        }
    }
    /* 全部发完 → 置 DONE，下一轮 tick 发 COMPLETE（竞态规避，见函数头） */
    if (g_send.sent >= g_send.size) {
        g_send.state = FILE_SEND_DONE;
    }
    return 0;
}

void file_reset_all(void)
{
    if (g_send.busy) {
        send_task_clear();  /* 发送中连接断 → 服务器自动取消并公告对端 */
    }
    /* 接收任务全部清理：删半成品（断线/新会话，残留文件不留） */
    for (size_t i = 0; i < FILE_MAX_RECV_PENDING; i++) {
        if (g_recv[i].busy) {
            recv_task_clear(&g_recv[i]);
        }
    }
}

/* ==================== 接收状态机 ==================== */

/* g_recv 数组定义见"发送状态机"小节顶部（file_reset_all 等先于本节使用）。
 * pending：busy && fp == NULL（公告已到，等 /accept）
 * active： busy && fp != NULL（已 /accept，收片中） */

/**
 * @brief 保存名存在性探针（file_unique_name 注入）：downloads/name 是否已存在
 * @param ctx 目录名（FILE_DOWNLOAD_DIR）
 */
static bool recv_name_taken(const char *name, void *ctx)
{
    const char *dir = (const char *)ctx;
    char path[512];
    if (file_join_path(dir, name, path, sizeof(path)) < 0) {
        return true;  /* 保守：路径拼不出视为冲突 */
    }
    return file_path_exists(path);
}

static file_recv_task_t *recv_task_find(uint32_t tid)
{
    for (size_t i = 0; i < FILE_MAX_RECV_PENDING; i++) {
        if (g_recv[i].busy && g_recv[i].tid == tid) {
            return &g_recv[i];
        }
    }
    return NULL;
}

/* 活动接收任务（单槽规则下至多 1 个） */
static file_recv_task_t *recv_task_active(void)
{
    for (size_t i = 0; i < FILE_MAX_RECV_PENDING; i++) {
        if (g_recv[i].busy && g_recv[i].fp != NULL) {
            return &g_recv[i];
        }
    }
    return NULL;
}

/* 第一个空闲槽；-1 表示 pending 已满 */
static int recv_task_slot(void)
{
    for (size_t i = 0; i < FILE_MAX_RECV_PENDING; i++) {
        if (!g_recv[i].busy) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief 清理接收任务：关句柄、删半成品文件、释放位图、清零
 * @note 完成校验通过需保留文件时，调用方先置空 save_name（recv_task_finish）
 */
static void recv_task_clear(file_recv_task_t *t)
{
    if (t->fp != NULL) {
        fclose(t->fp);
        t->fp = NULL;
    }
    if (t->save_name[0] != '\0') {
        char path[512];
        if (file_join_path(FILE_DOWNLOAD_DIR, t->save_name,
                           path, sizeof(path)) == 0) {
            remove(path);  /* 删半成品；文件不存在时无害 */
        }
    }
    free(t->bitmap);
    t->bitmap = NULL;
    memset(t, 0, sizeof(*t));
}

/**
 * @brief 完成校验（COMPLETE 帧与 complete 公告共用，幂等）：
 *        字节数一致 → 保留文件；不符 → 删除
 * @note 服务器无 chunk 确认（spec Further Notes），字节数校验是客户端
 *       自兜底——发送侧已知局限：竞态下末片丢失，此处负责发现并清理
 */
static void recv_task_finish(file_recv_task_t *t)
{
    bool ok = (t->received == t->size);
    char msg[512];
    if (ok) {
        snprintf(msg, sizeof(msg),
                 "文件 #%u 接收完成，已保存: %s/%s (%llu 字节)",
                 t->tid, FILE_DOWNLOAD_DIR, t->save_name,
                 (unsigned long long)t->received);
    } else {
        snprintf(msg, sizeof(msg),
                 "文件 #%u 不完整（%llu/%llu 字节），已删除。",
                 t->tid, (unsigned long long)t->received,
                 (unsigned long long)t->size);
    }
    ui_print(msg);
    if (ok) {
        t->save_name[0] = '\0';  /* 保留：recv_task_clear 不再删除 */
    }
    recv_task_clear(t);
}

/**
 * @brief 完成信号统一处理（COMPLETE 帧与 complete 公告共用，幂等）：
 *        活动任务 → 字节数校验（recv_task_finish）；pending 任务 → 直接
 *        清理（防御：正常服务器下 COMPLETE 只发给已 accept 的接收方，
 *        pending 收到是异常/边界场景，清理防任务滞留）
 */
static void recv_task_complete(uint32_t tid)
{
    file_recv_task_t *t = recv_task_find(tid);
    if (t == NULL) {
        return;  /* 双信号先到者已处理完，幂等 */
    }
    if (t->fp != NULL) {
        recv_task_finish(t);
    } else {
        recv_task_clear(t);
    }
}

/**
 * @brief 异常路径统一出口：发取消帧 + 提示 + 清理（删半成品）
 * @param why 提示文本前缀（追加 "#tid"）
 */
static void recv_task_abort(conn_t *conn, file_recv_task_t *t, const char *why)
{
    uint8_t frame[PROTO_HEADER_SIZE + 4];
    int n = protocol_build_tid(MSG_FILE_CANCEL, t->tid, frame, sizeof(frame));
    if (n >= 0) {
        (void)conn_send_all(conn, frame, n);  /* 连接已断时主循环会自己发现 */
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "%s（#%u）", why, t->tid);
    ui_print(msg);
    recv_task_clear(t);
}

/**
 * @brief 本地拒绝后回发 REJECT 帧并清理任务（自动拒绝路径统一出口）
 */
static void recv_task_reject(conn_t *conn, file_recv_task_t *t, const char *why)
{
    ui_print(why);
    uint8_t frame[PROTO_HEADER_SIZE + 4];
    int n = protocol_build_tid(MSG_FILE_REJECT, t->tid, frame, sizeof(frame));
    if (n >= 0) {
        (void)conn_send_all(conn, frame, n);
    }
    recv_task_clear(t);
}

/**
 * @brief incoming 公告：建 pending 任务（tid 只存在于公告文本，协议事实）
 *
 * pending 满（FILE_MAX_RECV_PENDING）时自动回发 REJECT——此时服务器
 * 传输记录已建（FT_PENDING），REJECT 有效。发送方收到拒绝公告后清理。
 * 后续到达的 INIT 帧无槽可关联会被忽略（recv_handle_init 找不到
 * 未填充任务；可能误填到在等帧的任务——概率极低的边角，接受）。
 */
static void recv_announce_incoming(conn_t *conn, uint32_t tid)
{
    int slot = recv_task_slot();
    if (slot < 0) {
        ui_print("接收任务已满，自动拒绝新传输。");
        uint8_t frame[PROTO_HEADER_SIZE + 4];
        int n = protocol_build_tid(MSG_FILE_REJECT, tid, frame, sizeof(frame));
        if (n >= 0) {
            (void)conn_send_all(conn, frame, n);
        }
        return;
    }
    file_recv_task_t *t = &g_recv[slot];
    memset(t, 0, sizeof(*t));
    t->busy = true;
    t->tid = tid;
    t->seq = g_recv_seq++;
    /* 公告文本含 sender/文件名/大小，但结构化字段由 INIT 帧填充
     * （公告为主信号建任务、INIT 帧为补充提供数据） */
}

/**
 * @brief INIT 帧：填充 pending 任务的发送方/文件名/大小（帧无 tid）
 *
 * 关联方式：填"公告到达序号最小且未填充"的任务。服务器线程池并行处理
 * 不同传输的 INIT（实测 2026-08-10）：各传输的公告与 INIT 帧相邻入队
 * （同一 worker 内顺序发送），但**传输之间的整体顺序可能交错**——
 * 用数组槽位顺序匹配会填错（charlie 的 INIT 帧填进 bob 的任务，
 * run B 实测 8MB 文件被 128KB 的 INIT 覆盖）。
 * seq 模拟"每对公告+帧相邻"的 FIFO 性质：公告到达序 == INIT 帧到达序，
 * 取 seq 最小者即对应同一次传输。解析失败按异常帧忽略（公告为主信号）。
 */
static void recv_handle_init(const uint8_t *payload, uint16_t plen)
{
    char sender[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    uint64_t size;
    if (file_parse_init_payload(payload, plen, sender, sizeof(sender),
                                filename, sizeof(filename), &size) < 0) {
        return;  /* 异常帧忽略 */
    }
    file_recv_task_t *best = NULL;
    for (size_t i = 0; i < FILE_MAX_RECV_PENDING; i++) {
        file_recv_task_t *t = &g_recv[i];
        if (t->busy && !t->inited &&
            (best == NULL || t->seq < best->seq)) {
            best = t;
        }
    }
    if (best != NULL) {
        best->inited = true;
        strcpy(best->sender, sender);
        strcpy(best->filename, filename);
        best->size = size;
    }
}

/**
 * @brief CHUNK 帧：按 offset fseek 定位写盘（服务器乱序中继，见 spec）
 *
 * 乱序到达是正常现象（实测相邻块交换/循环错位）；重复/越界 offset
 * 才视为错误（file_recv_span_check）→ 主动取消 + 删半成品。
 * tid 不匹配活动接收任务（未 accept 或已清理的迟到片）→ 忽略。
 */
static void recv_handle_chunk(conn_t *conn, const uint8_t *payload, uint16_t plen)
{
    if (plen < 12) {
        return;  /* 异常帧忽略 */
    }
    uint32_t tid = utils_read_u32_be(payload);
    uint64_t offset = utils_read_u64_be(payload + 4);
    const uint8_t *data = payload + 12;
    uint32_t len = (uint32_t)(plen - 12);

    file_recv_task_t *t = recv_task_find(tid);
    if (t == NULL || t->fp == NULL) {
        return;  /* 非活动接收任务，忽略 */
    }
    if (file_recv_span_check(t->size, t->bitmap, t->bitmap_len,
                             offset, len) < 0) {
        recv_task_abort(conn, t, "收到非法分片（重复/越界），已取消");
        return;
    }
    /* fseek 的 offset 参数为 long：LP64（WSL x86_64）下 64 位无碍；
     * 32 位 long 下 >2GB 文件偏移溢出——已知局限（本项目仅 WSL 构建） */
    if (fseek(t->fp, (long)offset, SEEK_SET) != 0 ||
        fwrite(data, 1, len, t->fp) != len) {
        recv_task_abort(conn, t, "写入文件失败，已取消");
        return;
    }
    (void)file_recv_span_mark(t->bitmap, t->bitmap_len, offset);
    t->received += len;
}

int file_cmd_accept(conn_t *conn, const char *line)
{
    const char *arg = line + strlen("/accept");
    uint32_t tid;
    if (commands_parse_tid_arg(arg, &tid) < 0) {
        ui_print("用法: /accept <tid>");
        return 0;
    }
    file_recv_task_t *t = recv_task_find(tid);
    if (t == NULL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "没有 #%u 的传输记录。", tid);
        ui_print(msg);
        return 0;
    }
    if (t->fp != NULL) {
        ui_print("该传输已在接收中。");
        return 0;
    }
    if (recv_task_active() != NULL) {
        /* 单槽规则（ADR-0002）：至多 1 个活动接收，第二个本地拒绝 */
        ui_print("已有进行中的接收任务，完成或取消后再试。");
        return 0;
    }
    if (!t->inited) {
        /* INIT 帧未到（公告先到帧后到，正常时序下用户不会这么快）——防御 */
        ui_print("传输详情未就绪，请稍后再试。");
        return 0;
    }
    if (t->size == 0) {
        /* 服务器不校验 size（spec Further Notes），异常声明直接拒绝 */
        recv_task_reject(conn, t, "文件大小为 0，已自动拒绝。");
        return 0;
    }

    /* 文件名 basename 化 + 唯一化（防目录穿越 + 同名加序号） */
    char base[MAX_FILENAME_LEN];
    if (file_basename_sanitize(t->filename, base, sizeof(base)) < 0) {
        recv_task_reject(conn, t, "文件名非法，已自动拒绝。");
        return 0;
    }
    if (file_unique_name(base, recv_name_taken, (void *)FILE_DOWNLOAD_DIR,
                         t->save_name, sizeof(t->save_name)) < 0) {
        recv_task_reject(conn, t, "无法生成保存文件名，已自动拒绝。");
        return 0;
    }

    /* 接收目录（不存在则创建；已存在且是目录则忽略 EEXIST） */
    if (mkdir(FILE_DOWNLOAD_DIR, 0755) != 0 && errno != EEXIST) {
        recv_task_reject(conn, t, "无法创建接收目录 downloads/，已自动拒绝。");
        return 0;
    }

    /* 片号去重位图：按 size 分配（size 巨大时 malloc 失败 → 拒绝）。
     * 先挂到任务上，后续所有失败路径由 recv_task_reject/clear 统一释放，
     * 避免手动 free 遗漏（code-review 修复，2026-08-10） */
    uint64_t chunks = file_chunk_count(t->size, FILE_CHUNK_DATA_MAX);
    t->bitmap_len = (size_t)(chunks / 8) + 1;
    t->bitmap = malloc(t->bitmap_len);
    if (t->bitmap == NULL) {
        recv_task_reject(conn, t, "文件过大，无法接收，已自动拒绝。");
        return 0;
    }
    memset(t->bitmap, 0, t->bitmap_len);

    char path[512];
    if (file_join_path(FILE_DOWNLOAD_DIR, t->save_name,
                       path, sizeof(path)) < 0) {
        recv_task_reject(conn, t, "保存路径过长，已自动拒绝。");
        return 0;
    }
    t->fp = fopen(path, "wb");
    if (t->fp == NULL) {
        recv_task_reject(conn, t, "无法创建文件，已自动拒绝。");
        return 0;
    }

    /* 发送接受帧（失败 = 连接已断，返回 -1 让事件循环退出） */
    uint8_t frame[PROTO_HEADER_SIZE + 4];
    int n = protocol_build_tid(MSG_FILE_ACCEPT, tid, frame, sizeof(frame));
    if (n < 0 || conn_send_all(conn, frame, n) < 0) {
        recv_task_clear(t);  /* bitmap 已挂任务，统一释放 */
        return -1;
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "已接受 #%u，保存到: %s/%s",
             tid, FILE_DOWNLOAD_DIR, t->save_name);
    ui_print(msg);
    return 0;
}

int file_cmd_reject(conn_t *conn, const char *line)
{
    const char *arg = line + strlen("/reject");
    uint32_t tid;
    if (commands_parse_tid_arg(arg, &tid) < 0) {
        ui_print("用法: /reject <tid>");
        return 0;
    }
    file_recv_task_t *t = recv_task_find(tid);
    if (t == NULL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "没有 #%u 的传输记录。", tid);
        ui_print(msg);
        return 0;
    }
    if (t->fp != NULL) {
        /* 已活动的传输服务器只接受取消（REJECT 仅对 FT_PENDING 有效） */
        char msg[96];
        snprintf(msg, sizeof(msg), "#%u 正在接收中，请用 /cancel 取消。", tid);
        ui_print(msg);
        return 0;
    }

    uint8_t frame[PROTO_HEADER_SIZE + 4];
    int n = protocol_build_tid(MSG_FILE_REJECT, tid, frame, sizeof(frame));
    if (n < 0) {
        return -1;
    }
    if (conn_send_all(conn, frame, n) < 0) {
        return -1;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "已拒绝 #%u。", tid);
    ui_print(msg);
    recv_task_clear(t);  /* 清理：任务删除（尚无半成品，未落盘） */
    return 0;
}
