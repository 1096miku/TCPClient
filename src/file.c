#include "file.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/* ==================== 发送状态机 ==================== */

/* 发送任务：全局单槽（ADR-0002：单槽限并发），模块级状态 */
static file_send_task_t g_send;

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
    /* 接收任务匹配在票 03 扩展 */
    char msg[64];
    snprintf(msg, sizeof(msg), "没有 #%u 的传输记录。", tid);
    ui_print(msg);
    return 0;
}

void file_handle_announcement(const char *text)
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
    case FILE_ANN_CANCELLED:
    case FILE_ANN_CANCELLED_DISC:
        if (send_task_matches(tid)) {
            send_task_clear();
        }
        break;
    case FILE_ANN_COMPLETE:
        /* 发送方完成确认（COMPLETE 帧只发接收方，发送方只有公告）：
         * WAIT_COMPLETE → 清理，槽位释放 */
        if (send_task_matches(tid) && g_send.state == FILE_SEND_WAIT_COMPLETE) {
            send_task_clear();
        }
        break;
    default:
        break;  /* RECV_PENDING 由接收侧（票 03）处理 */
    }
}

void file_handle_frame(conn_t *conn, uint8_t type,
                       const uint8_t *payload, uint16_t plen)
{
    (void)conn;  /* 票 02 发送方处理不需要回发（接收侧票 03 用到） */
    if (plen != 4) {
        return;  /* 控制帧载荷恒为 4B tid，非 4B 视为异常忽略 */
    }
    uint32_t tid = utils_read_u32_be(payload);
    switch (type) {
    case MSG_FILE_ACCEPT:
        if (send_task_matches(tid) && g_send.state == FILE_SEND_PENDING) {
            g_send.state = FILE_SEND_ACTIVE;  /* 与公告幂等（公告先到，此分支常被跳过） */
        }
        break;
    case MSG_FILE_REJECT:
    case MSG_FILE_CANCEL:
        if (send_task_matches(tid)) {
            send_task_clear();
        }
        break;
    default:
        break;  /* INIT/CHUNK/COMPLETE 由接收侧（票 03）处理 */
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
        /* 300 只来自文件传输操作（错误文本 "Invalid transfer state"） */
        if (g_send.busy) {
            send_task_clear();
        }
    }
}

bool file_send_active(void)
{
    return g_send.busy && (g_send.state == FILE_SEND_ACTIVE ||
                           g_send.state == FILE_SEND_DONE);
}

/**
 * @brief 轮询发送：每轮 ≤FILE_CHUNKS_PER_ROUND 片（≈256KB = 服务器写缓冲上限）
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
    /* 接收任务清理在票 03 扩展 */
}
