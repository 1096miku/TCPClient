/* 命令解析单元测试：/priv 参数切分（无框架，ctest 注册） */
#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "utils.h"

static int failures = 0;

#define CHECK(cond, name) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] %s\n", name); \
            failures++; \
        } \
    } while (0)

/* ---------- 用例 1：黄金切分 ---------- */
static void test_priv_basic(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_priv("/priv bob 你好", target, sizeof(target), &msg);
    CHECK(r == 0, "基本切分成功");
    CHECK(strcmp(target, "bob") == 0, "target 切出 bob");
    CHECK(msg != NULL && strcmp(msg, "你好") == 0, "message 切出 你好");
}

/* ---------- 用例 2：多余空白（target 前/中） ---------- */
static void test_priv_extra_ws(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_priv("/priv   bob   你好", target, sizeof(target), &msg);
    CHECK(r == 0, "多余空白仍成功");
    CHECK(strcmp(target, "bob") == 0, "target 切出 bob（多空白）");
    CHECK(msg != NULL && strcmp(msg, "你好") == 0, "message 跳过前导空白");
}

/* ---------- 用例 3：消息内部空格保留 ---------- */
static void test_priv_msg_spaces(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_priv("/priv bob 你好 世界", target, sizeof(target), &msg);
    CHECK(r == 0, "内部空格切分成功");
    CHECK(msg != NULL && strcmp(msg, "你好 世界") == 0, "message 内部空格保留");
}

/* ---------- 用例 4：缺消息 ---------- */
static void test_priv_missing_msg(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    CHECK(commands_parse_priv("/priv bob", target, sizeof(target), &msg) == -1,
          "缺消息返回 -1");
}

/* ---------- 用例 5：缺 target ---------- */
static void test_priv_missing_target(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    CHECK(commands_parse_priv("/priv", target, sizeof(target), &msg) == -1,
          "缺 target 返回 -1");
    CHECK(commands_parse_priv("/priv   ", target, sizeof(target), &msg) == -1,
          "只有空白返回 -1");
}

/* ---------- 用例 6：target 超长拒绝 ---------- */
static void test_priv_target_too_long(void)
{
    char target[MAX_USERNAME_LEN];
    const char *msg = NULL;
    char line[128];
    /* 32 字符 target 超上限（MAX_USERNAME_LEN-1=31） */
    memset(line, 'x', 32);
    line[32] = '\0';
    char input[200];
    snprintf(input, sizeof(input), "/priv %s hi", line);
    CHECK(commands_parse_priv(input, target, sizeof(target), &msg) == -1,
          "超长 target 返回 -1");
}

int main(void)
{
    test_priv_basic();
    test_priv_extra_ws();
    test_priv_msg_spaces();
    test_priv_missing_msg();
    test_priv_missing_target();
    test_priv_target_too_long();

    if (failures > 0) {
        fprintf(stderr, "test_commands: %d 个用例失败\n", failures);
        return 1;
    }
    printf("test_commands: 全部通过\n");
    return 0;
}
