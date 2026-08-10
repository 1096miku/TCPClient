/* 编解码单元测试：黄金字节 + 增量分片解析 + 错误路径（无框架，ctest 注册） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static int failures = 0;

#define CHECK(cond, name) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] %s\n", name); \
            failures++; \
        } \
    } while (0)

/* ---------- 用例 1：黄金字节 text1 ---------- */
static void test_golden_text1(void)
{
    uint8_t buf[64];
    static const uint8_t golden[] = {
        0xCA, 0xFE, 0x02, 0x00, 0x06, 'h', 'e', 'l', 'l', 'o', 0x00
    };
    int n = protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));
    CHECK(n == 11, "text1 返回 11 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "text1 黄金字节一致");
}

/* ---------- 用例 2：黄金字节 raw frame ---------- */
static void test_golden_frame(void)
{
    uint8_t buf[64];
    static const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t golden[] = {
        0xCA, 0xFE, 0x01, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04
    };
    int n = protocol_build_frame(MSG_AUTH, data, sizeof(data), buf, sizeof(buf));
    CHECK(n == 9, "raw frame 返回 9 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "raw frame 黄金字节一致");
}

/* ---------- 用例 3：text1 构造后 parse 往返 ---------- */
static void test_text1_roundtrip(void)
{
    uint8_t buf[64];
    int n = protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n, "parse 消耗整帧");
    CHECK(type == MSG_CHAT, "type 往返一致");
    CHECK(plen == 6, "payload_len 含尾随 NUL");
    CHECK(memcmp(payload, "hello\0", 6) == 0, "payload 往返一致");
}

/* ---------- 用例 4：逐字节分片（0..10 字节返回 0，第 11 字节起返回 11） ---------- */
static void test_fragmented_bytewise(void)
{
    uint8_t buf[64];
    int n = protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));
    for (int i = 0; i < n; i++) {
        uint8_t type;
        const uint8_t *payload;
        uint16_t plen;
        int r = protocol_parse(buf, i, &type, &payload, &plen);
        CHECK(r == 0, "分片不足返回 0（等待更多数据）");
    }
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n, "完整帧返回消耗字节数");
    CHECK(plen == 6, "完整帧 payload_len 正确");
}

/* ---------- 用例 5：双帧粘连 ---------- */
static void test_two_frames_back_to_back(void)
{
    uint8_t buf[128];
    int n1 = protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));
    int n2 = protocol_build_text1(MSG_CHAT, "world", buf + n1, sizeof(buf) - n1);

    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r1 = protocol_parse(buf, n1 + n2, &type, &payload, &plen);
    CHECK(r1 == n1, "第一帧消耗 n1 字节");
    int r2 = protocol_parse(buf + r1, n1 + n2 - r1, &type, &payload, &plen);
    CHECK(r2 == n2, "第二帧消耗 n2 字节");
    CHECK(type == MSG_CHAT && plen == 6, "第二帧内容正确");
    CHECK(memcmp(payload, "world\0", 6) == 0, "第二帧 payload 正确");
}

/* ---------- 用例 6：半头（不足 5 字节） ---------- */
static void test_partial_header(void)
{
    uint8_t buf[64];
    (void)protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));  /* 仅填充缓冲 */
    for (int i = 0; i < PROTO_HEADER_SIZE; i++) {
        uint8_t type;
        const uint8_t *payload;
        uint16_t plen;
        int r = protocol_parse(buf, i, &type, &payload, &plen);
        CHECK(r == 0, "半头返回 0");
    }
}

/* ---------- 用例 7：半载荷（头完整、载荷只有一半） ---------- */
static void test_partial_payload(void)
{
    uint8_t buf[64];
    (void)protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));  /* 仅填充缓冲 */
    int half = PROTO_HEADER_SIZE + 3;  /* 载荷 6 字节只给 3 */
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, half, &type, &payload, &plen);
    CHECK(r == 0, "半载荷返回 0");
}

/* ---------- 用例 8：坏 magic ---------- */
static void test_bad_magic(void)
{
    uint8_t buf[64];
    int n = protocol_build_text1(MSG_CHAT, "hello", buf, sizeof(buf));
    buf[0] = 0xAA;  /* 破坏 magic */
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == -1, "坏 magic 返回 -1");

    /* 整缓冲无 magic：全 0xFF */
    uint8_t junk[8];
    memset(junk, 0xFF, sizeof(junk));
    r = protocol_parse(junk, sizeof(junk), &type, &payload, &plen);
    CHECK(r == -1, "无 magic 返回 -1");
}

/* ---------- 用例 9：零载荷帧 ---------- */
static void test_zero_payload(void)
{
    uint8_t buf[64];
    static const uint8_t golden[] = {0xCA, 0xFE, 0x08, 0x00, 0x00};
    int n = protocol_build_frame(MSG_USERS, NULL, 0, buf, sizeof(buf));
    CHECK(n == 5, "零载荷帧 5 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "零载荷帧黄金字节一致");

    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == 5 && type == MSG_USERS && plen == 0, "零载荷帧解析正确");
    CHECK(payload == NULL, "零载荷 payload 为 NULL");
}

/* ---------- 用例 10：text2 往返（alice\0pw\0） ---------- */
static void test_text2_roundtrip(void)
{
    uint8_t buf[64];
    int n = protocol_build_text2(MSG_AUTH, "alice", "pw", buf, sizeof(buf));
    CHECK(n == 14, "text2 返回 14 字节（5 头 + 9 载荷）");

    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n, "text2 parse 消耗整帧");
    CHECK(type == MSG_AUTH, "text2 type 正确");
    CHECK(plen == 9, "text2 payload_len = 9");
    CHECK(memcmp(payload, "alice\0pw\0", 9) == 0, "text2 payload 布局正确");
}

/* ---------- 用例 11：error 帧往返 ---------- */
static void test_error_roundtrip(void)
{
    uint8_t buf[128];
    const char *msg = "Invalid username or password";
    int n = protocol_build_error(ERR_BAD_CREDENTIALS, msg, buf, sizeof(buf));

    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n, "error parse 消耗整帧");
    CHECK(type == MSG_ERROR, "error type 固定 0x10");
    CHECK(utils_read_u16_be(payload) == ERR_BAD_CREDENTIALS, "error code 大端正确");
    CHECK(payload[2] == '\0', "code 后分隔 NUL");
    CHECK(strcmp((const char *)payload + 3, msg) == 0, "error message 正确");
}

/* ---------- 用例 12：out_cap 不足 ---------- */
static void test_buffer_too_small(void)
{
    uint8_t buf[8];
    static const uint8_t data[] = {1, 2, 3, 4, 5};
    int n = protocol_build_frame(MSG_AUTH, data, sizeof(data), buf, sizeof(buf));
    CHECK(n == -1, "out_cap 不足返回 -1");

    n = protocol_build_frame(MSG_AUTH, data, 3, buf, sizeof(buf));
    CHECK(n == 8, "out_cap 恰好够时成功");
}

/* ---------- 用例 13：超长载荷（> 1MB） ---------- */
static void test_oversized_payload(void)
{
    size_t big = PROTO_MAX_PAYLOAD + 2;
    char *text = malloc(big);
    memset(text, 'a', big - 1);
    text[big - 1] = '\0';

    uint8_t buf[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    int n = protocol_build_text1(MSG_CHAT, text, buf, sizeof(buf));
    CHECK(n == -1, "text1 超长返回 -1");

    n = protocol_build_raw(MSG_CHAT, (const uint8_t *)text, (int)big, buf, sizeof(buf));
    CHECK(n == -1, "raw 超长返回 -1");

    free(text);
}

/* ---------- 用例 15：黄金字节 text3（INIT 三段载荷） ---------- */
static void test_golden_text3(void)
{
    uint8_t buf[64];
    static const uint8_t golden[] = {
        0xCA, 0xFE, 0x09, 0x00, 0x0E,
        'b', 'o', 'b', 0x00,
        'a', '.', 't', 'x', 't', 0x00,
        '1', '0', '0', 0x00
    };
    int n = protocol_build_text3(MSG_FILE_INIT, "bob", "a.txt", "100",
                                 buf, sizeof(buf));
    CHECK(n == 19, "text3 返回 19 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "text3 黄金字节一致");

    /* 解析往返：三段 NUL 位置 */
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n && type == MSG_FILE_INIT && plen == 14, "text3 parse 往返正确");
    CHECK(memcmp(payload, "bob\0a.txt\0" "100\0", 14) == 0, "text3 载荷布局正确");
}

/* ---------- 用例 16：黄金字节 tid 帧（4B 大端） ---------- */
static void test_golden_tid(void)
{
    uint8_t buf[64];
    static const uint8_t golden[] = {
        0xCA, 0xFE, 0x0A, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04
    };
    int n = protocol_build_tid(MSG_FILE_ACCEPT, 0x01020304, buf, sizeof(buf));
    CHECK(n == 9, "tid 帧 9 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "tid 帧黄金字节一致");

    /* 解析往返 */
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf, n, &type, &payload, &plen);
    CHECK(r == n && type == MSG_FILE_ACCEPT && plen == 4, "tid 帧 parse 正确");
    CHECK(utils_read_u32_be(payload) == 0x01020304, "tid 大端正确");
}

/* ---------- 用例 17：黄金字节 chunk（tid+offset+data） ---------- */
static void test_golden_chunk(void)
{
    uint8_t buf[64];
    static const uint8_t data[] = {'A', 'B'};
    static const uint8_t golden[] = {
        0xCA, 0xFE, 0x0C, 0x00, 0x0E,
        0x00, 0x00, 0x00, 0x01,                     /* tid=1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* offset=0 (8B) */
        'A', 'B'
    };
    int n = protocol_build_chunk(1, 0, data, sizeof(data), buf, sizeof(buf));
    CHECK(n == 19, "chunk 帧 19 字节");
    CHECK(memcmp(buf, golden, sizeof(golden)) == 0, "chunk 黄金字节一致");

    /* data_len=0 边界 */
    uint8_t buf0[64];
    int n0 = protocol_build_chunk(1, 0, NULL, 0, buf0, sizeof(buf0));
    CHECK(n0 == 17, "chunk 空数据 17 字节");
    uint8_t type;
    const uint8_t *payload;
    uint16_t plen;
    int r = protocol_parse(buf0, n0, &type, &payload, &plen);
    CHECK(r == n0 && plen == 12, "空数据 chunk parse 正确");
    CHECK(utils_read_u64_be(payload + 4) == 0, "offset 大端正确");

    /* out_cap 不足 */
    uint8_t small[10];
    CHECK(protocol_build_chunk(1, 0, data, sizeof(data), small, sizeof(small)) == -1,
          "chunk out_cap 不足返回 -1");
}

/* ---------- 用例 14：utils 附测 ---------- */
static void test_utils_helpers(void)
{
    char s[32];
    utils_strncpy(s, "hello world", sizeof(s));
    CHECK(strcmp(s, "hello world") == 0, "strncpy 正常拷贝");

    char t[8];
    size_t r = utils_strncpy(t, "hello world", sizeof(t));
    CHECK(strcmp(t, "hello w") == 0, "strncpy 截断并 NUL 结尾");
    CHECK(r == 11, "strncpy 返回源长度");

    char line[] = "hi\r\n";
    utils_trim_newline(line);
    CHECK(strcmp(line, "hi") == 0, "trim_newline 剥 \r\n");

    static const uint8_t be[] = {0xCA, 0xFE};
    CHECK(utils_read_u16_be(be) == 0xCAFE, "read_u16_be 大端正确");

    /* utils_parse_u64_range（M4 超集函数） */
    uint64_t u = 0;
    CHECK(utils_parse_u64_range("123", "123" + 3, &u) == 0 && u == 123,
          "parse_u64_range 基本解析");
    CHECK(utils_parse_u64_range("12a", "12a" + 3, &u) == -1,
          "parse_u64_range 非数字拒绝");
    CHECK(utils_parse_u64_range("", "", &u) == -1, "parse_u64_range 空范围拒绝");
    CHECK(utils_parse_u64_range("18446744073709551615",
                                "18446744073709551615" + 20, &u) == 0 &&
          u == UINT64_MAX, "parse_u64_range 上限可解析");
    CHECK(utils_parse_u64_range("18446744073709551616",
                                "18446744073709551616" + 20, &u) == -1,
          "parse_u64_range 溢出拒绝");
}

int main(void)
{
    test_golden_text1();
    test_golden_frame();
    test_text1_roundtrip();
    test_fragmented_bytewise();
    test_two_frames_back_to_back();
    test_partial_header();
    test_partial_payload();
    test_bad_magic();
    test_zero_payload();
    test_text2_roundtrip();
    test_error_roundtrip();
    test_buffer_too_small();
    test_oversized_payload();
    test_utils_helpers();
    test_golden_text3();
    test_golden_tid();
    test_golden_chunk();

    if (failures == 0) {
        printf("test_protocol: all %d checks passed\n", 17);
        return EXIT_SUCCESS;
    }
    printf("test_protocol: %d check(s) FAILED\n", failures);
    return EXIT_FAILURE;
}
