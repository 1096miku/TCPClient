/* 文件传输域纯函数单元测试（无框架，ctest 注册） */
#include <stdio.h>
#include <string.h>

#include "file.h"
#include "utils.h"

static int failures = 0;

#define CHECK(cond, name) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] %s\n", name); \
            failures++; \
        } \
    } while (0)

/* ---------- file_parse_announcement：7 种公告黄金 ---------- */

static void test_announcement_send_pending(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text =
        "[Server] File transfer #5: 'a.txt' (100 bytes) → bob. Waiting for acceptance...";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_SEND_PENDING && tid == 5,
          "等待接受公告 → SEND_PENDING tid=5");
}

static void test_announcement_recv_pending(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text =
        "[Server] Incoming file transfer #7 from alice: 'b.txt' (200 bytes). Accept or reject?";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_RECV_PENDING && tid == 7,
          "收到传输公告 → RECV_PENDING tid=7");
}

static void test_announcement_accepted(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text =
        "[Server] File transfer #5 accepted by bob. Start sending chunks.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_ACCEPTED && tid == 5,
          "接受公告 → ACCEPTED tid=5");
}

static void test_announcement_rejected(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] File transfer #5 rejected by bob.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_REJECTED && tid == 5,
          "拒绝公告 → REJECTED tid=5");
}

static void test_announcement_complete(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text =
        "[Server] File transfer #5 complete: 'a.txt' (100/100 bytes transferred).";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_COMPLETE && tid == 5,
          "完成公告 → COMPLETE tid=5");
}

static void test_announcement_cancelled(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] File transfer #5 cancelled by bob.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_CANCELLED && tid == 5,
          "取消公告 → CANCELLED tid=5");
}

static void test_announcement_cancelled_disc(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] File transfer #5 cancelled: bob disconnected.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_CANCELLED_DISC && tid == 5,
          "断线取消公告 → CANCELLED_DISC tid=5");
}

/* ---------- 无前缀可识别 / 非文件公告拒绝 ---------- */

static void test_announcement_no_prefix(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "File transfer #5 accepted by bob. Start sending chunks.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == 0 && kind == FILE_ANN_ACCEPTED && tid == 5,
          "无 [Server] 前缀也可识别");
}

static void test_announcement_welcome(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] Welcome alice!";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == -1 && kind == FILE_ANN_NONE, "普通公告 → 非文件事件");
}

static void test_announcement_chat_text(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] alice: File transfer #5 failed";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == -1 && kind == FILE_ANN_NONE, "聊天文本含 File transfer 不误匹配");
}

static void test_announcement_no_digits(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *text = "[Server] File transfer #x accepted by bob.";
    int r = file_parse_announcement(text, &kind, &tid);
    CHECK(r == -1 && kind == FILE_ANN_NONE, "# 后无数字 → 非文件事件");
}

static void test_announcement_tid_boundary(void)
{
    file_ann_kind_t kind = FILE_ANN_NONE;
    uint32_t tid = 0;
    const char *max = "[Server] File transfer #4294967295 accepted by bob.";
    CHECK(file_parse_announcement(max, &kind, &tid) == 0 && tid == 4294967295u,
          "tid=UINT32_MAX 可解析");
    const char *over = "[Server] File transfer #4294967296 accepted by bob.";
    CHECK(file_parse_announcement(over, &kind, &tid) == -1,
          "tid 超 uint32 → 非文件事件");
}

/* ---------- file_parse_init_payload ---------- */

static void test_init_payload_golden(void)
{
    static const uint8_t payload[] = "alice\0a.txt\0" "100\0";
    char sender[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    uint64_t size = 0;
    int r = file_parse_init_payload(payload, (uint16_t)(sizeof(payload) - 1),
                                    sender, sizeof(sender),
                                    filename, sizeof(filename), &size);
    CHECK(r == 0, "INIT 载荷黄金解析成功");
    CHECK(strcmp(sender, "alice") == 0, "发送方=alice");
    CHECK(strcmp(filename, "a.txt") == 0, "文件名=a.txt");
    CHECK(size == 100, "大小=100");
}

static void test_init_payload_missing(void)
{
    static const uint8_t p1[] = "alice\0a.txt";  /* 无第三段 */
    static const uint8_t p2[] = "alice";         /* 单段 */
    char sender[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    uint64_t size = 0;
    CHECK(file_parse_init_payload(p1, (uint16_t)(sizeof(p1) - 1),
                                  sender, sizeof(sender),
                                  filename, sizeof(filename), &size) == -1,
          "缺第三段返回 -1");
    CHECK(file_parse_init_payload(p2, (uint16_t)(sizeof(p2) - 1),
                                  sender, sizeof(sender),
                                  filename, sizeof(filename), &size) == -1,
          "单段返回 -1");
}

static void test_init_payload_too_long(void)
{
    char big[48];
    memset(big, 'u', 32);
    big[32] = '\0';
    char buf[80];
    size_t o = 0;
    memcpy(buf + o, big, 33);  o += 33;  /* 32 字符 + NUL（超上限 31） */
    memcpy(buf + o, "a.txt\0", 6);  o += 6;
    memcpy(buf + o, "100\0", 4);  o += 4;
    char sender[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    uint64_t size = 0;
    CHECK(file_parse_init_payload((const uint8_t *)buf, (uint16_t)o,
                                  sender, sizeof(sender),
                                  filename, sizeof(filename), &size) == -1,
          "发送方超长返回 -1");
}

static void test_init_payload_bad_size(void)
{
    static const uint8_t p1[] = "alice\0a.txt\0" "abc\0";
    static const uint8_t p2[] = "alice\0a.txt\0" "12a\0";
    char sender[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    uint64_t size = 0;
    CHECK(file_parse_init_payload(p1, (uint16_t)(sizeof(p1) - 1),
                                  sender, sizeof(sender),
                                  filename, sizeof(filename), &size) == -1,
          "大小非数字返回 -1");
    CHECK(file_parse_init_payload(p2, (uint16_t)(sizeof(p2) - 1),
                                  sender, sizeof(sender),
                                  filename, sizeof(filename), &size) == -1,
          "大小混合返回 -1");
}

/* ---------- file_basename_sanitize ---------- */

static void test_basename_posix_path(void)
{
    char out[MAX_FILENAME_LEN];
    int r = file_basename_sanitize("a/b/c.txt", out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "c.txt") == 0, "POSIX 路径取 basename");
}

static void test_basename_win_path(void)
{
    char out[MAX_FILENAME_LEN];
    int r = file_basename_sanitize("C:\\dir\\f.txt", out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "f.txt") == 0, "反斜杠路径取 basename");
}

static void test_basename_no_sep(void)
{
    char out[MAX_FILENAME_LEN];
    int r = file_basename_sanitize("plain.txt", out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "plain.txt") == 0, "无分隔符原名");
}

static void test_basename_dot(void)
{
    char out[MAX_FILENAME_LEN];
    CHECK(file_basename_sanitize(".", out, sizeof(out)) == -1, "'.' 拒绝");
    CHECK(file_basename_sanitize("..", out, sizeof(out)) == -1, "'..' 拒绝");
    CHECK(file_basename_sanitize("", out, sizeof(out)) == -1, "空名拒绝");
}

static void test_basename_control(void)
{
    char out[MAX_FILENAME_LEN];
    int r = file_basename_sanitize("a\tb", out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "a_b") == 0, "控制字符替换为 _");
}

static void test_basename_too_long(void)
{
    char out[MAX_FILENAME_LEN];
    char big[320];
    memset(big, 'f', 300);
    big[300] = '\0';
    CHECK(file_basename_sanitize(big, out, sizeof(out)) == -1, "超长拒绝");
}

/* ---------- file_unique_name（假探针注入，不碰文件系统） ---------- */

typedef struct {
    const char *taken[16];
    int n;
} taken_set_t;

static bool probe_exists(const char *name, void *ctx)
{
    taken_set_t *t = (taken_set_t *)ctx;
    for (int i = 0; i < t->n; i++) {
        if (strcmp(name, t->taken[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void test_unique_no_conflict(void)
{
    taken_set_t set = {{0}, 0};
    char out[MAX_FILENAME_LEN];
    int r = file_unique_name("a.txt", probe_exists, &set, out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "a.txt") == 0, "无冲突用原名");
}

static void test_unique_conflict1(void)
{
    taken_set_t set = {{"a.txt"}, 1};
    char out[MAX_FILENAME_LEN];
    int r = file_unique_name("a.txt", probe_exists, &set, out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "a.txt (1)") == 0, "被占 1 次 → base (1)");
}

static void test_unique_conflict3(void)
{
    taken_set_t set = {{"a.txt", "a.txt (1)", "a.txt (2)"}, 3};
    char out[MAX_FILENAME_LEN];
    int r = file_unique_name("a.txt", probe_exists, &set, out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "a.txt (3)") == 0, "被占 3 次 → base (3)");
}

static void test_unique_buf_small(void)
{
    taken_set_t set = {{0}, 0};
    char out[4];
    CHECK(file_unique_name("longname.txt", probe_exists, &set, out, sizeof(out)) == -1,
          "缓冲不足返回 -1");
}

/* ---------- file_chunk_count / file_chunk_plan ---------- */

static void test_chunk_zero(void)
{
    CHECK(file_chunk_count(0, FILE_CHUNK_DATA_MAX) == 0, "size=0 → 0 片");
}

static void test_chunk_one(void)
{
    CHECK(file_chunk_count(FILE_CHUNK_DATA_MAX, FILE_CHUNK_DATA_MAX) == 1,
          "size=65500 → 1 片");
    uint64_t off = 0;
    uint32_t len = 0;
    CHECK(file_chunk_plan(FILE_CHUNK_DATA_MAX, FILE_CHUNK_DATA_MAX, 0, &off, &len),
          "单片 plan 成功");
    CHECK(off == 0 && len == FILE_CHUNK_DATA_MAX, "单片 offset/len 正确");
}

static void test_chunk_two(void)
{
    uint64_t size = FILE_CHUNK_DATA_MAX + 1;
    CHECK(file_chunk_count(size, FILE_CHUNK_DATA_MAX) == 2, "size=65501 → 2 片");
    uint64_t off = 0;
    uint32_t len = 0;
    CHECK(file_chunk_plan(size, FILE_CHUNK_DATA_MAX, 1, &off, &len), "末片 plan 成功");
    CHECK(off == FILE_CHUNK_DATA_MAX && len == 1, "末片 offset=65500 len=1");
}

static void test_chunk_boundary(void)
{
    uint64_t size = FILE_CHUNK_DATA_MAX * 2;  /* 131000，整除 */
    CHECK(file_chunk_count(size, FILE_CHUNK_DATA_MAX) == 2, "整除 → 2 片");
    uint64_t off = 0;
    uint32_t len = 0;
    CHECK(!file_chunk_plan(size, FILE_CHUNK_DATA_MAX, 2, &off, &len),
          "index 越界 → false");
}

/* ---------- file_recv_span_check / file_recv_span_mark ----------
 * 接收分片去重（bitmap 按片号，片号 = offset / FILE_CHUNK_DATA_MAX）：
 * 服务器多线程转发可能乱序，乱序合法；重复/越界/非对齐视为错误。
 */

static void test_span_basic(void)
{
    uint64_t size = 200000;  /* 4 片：65500+65500+65500+3500，bitmap 1 字节足够 */
    uint8_t bm[8] = {0};
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 65500) == 0, "首片合法");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 0) == 0, "首片记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 65500) == -1, "重复片 → -1");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 65500, 65500) == 0, "乱序片合法");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 65500) == 0, "乱序片记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 131000, 3500) == 0, "末片合法");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 131000) == 0, "末片记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 131000, 3500) == -1, "末片重复 → -1");
}

static void test_span_boundary(void)
{
    uint64_t size = 65500;  /* 1 片 */
    uint8_t bm[2] = {0};
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 65500) == 0, "整片合法");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 0) == 0, "整片记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 65501) == -1, "len 越界 → -1");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 65500, 0) == -1, "offset>=size → -1");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 1, 100) == -1, "非对齐 offset → -1");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 0) == -1, "len=0 → -1");
    CHECK(file_recv_span_check(0, bm, sizeof(bm), 0, 100) == -1, "size=0 → -1");
}

static void test_span_bitmap_too_small(void)
{
    /* 300 片 → 38 字节 bitmap，只给 4 字节：片 200 的 bit 落在 bitmap 之外 */
    uint64_t size = (uint64_t)65500 * 300;
    uint8_t bm[4] = {0};
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 200 * 65500ull, 65500) == -1,
          "bitmap 长度不足 → -1");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 200 * 65500ull) == -1, "mark 越界 → -1");
}

static void test_span_ooo_mutual(void)
{
    uint64_t size = 131000;  /* 2 片 */
    uint8_t bm[1] = {0};
    CHECK(file_recv_span_mark(bm, sizeof(bm), 65500) == 0, "片1 先记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 0, 65500) == 0, "片0 乱序仍合法");
    CHECK(file_recv_span_mark(bm, sizeof(bm), 0) == 0, "片0 记录");
    CHECK(file_recv_span_check(size, bm, sizeof(bm), 65500, 65500) == -1, "片1 重复 → -1");
}

/* ---------- file_join_path ---------- */

static void test_join_basic(void)
{
    char out[512];
    int r = file_join_path("downloads", "a.txt", out, sizeof(out));
    CHECK(r == 0 && strcmp(out, "downloads/a.txt") == 0, "基本拼接");
}

static void test_join_too_long(void)
{
    char out[16];
    CHECK(file_join_path("downloads", "very-long-name.txt", out, sizeof(out)) == -1,
          "超长返回 -1");
}

int main(void)
{
    test_announcement_send_pending();
    test_announcement_recv_pending();
    test_announcement_accepted();
    test_announcement_rejected();
    test_announcement_complete();
    test_announcement_cancelled();
    test_announcement_cancelled_disc();
    test_announcement_no_prefix();
    test_announcement_welcome();
    test_announcement_chat_text();
    test_announcement_no_digits();
    test_announcement_tid_boundary();
    test_init_payload_golden();
    test_init_payload_missing();
    test_init_payload_too_long();
    test_init_payload_bad_size();
    test_basename_posix_path();
    test_basename_win_path();
    test_basename_no_sep();
    test_basename_dot();
    test_basename_control();
    test_basename_too_long();
    test_unique_no_conflict();
    test_unique_conflict1();
    test_unique_conflict3();
    test_unique_buf_small();
    test_chunk_zero();
    test_chunk_one();
    test_chunk_two();
    test_chunk_boundary();
    test_span_basic();
    test_span_boundary();
    test_span_bitmap_too_small();
    test_span_ooo_mutual();
    test_join_basic();
    test_join_too_long();

    if (failures > 0) {
        fprintf(stderr, "test_file: %d 个用例失败\n", failures);
        return 1;
    }
    printf("test_file: 全部通过\n");
    return 0;
}
