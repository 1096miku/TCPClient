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

/* ---------- 用例 7：gmsg 黄金切分 ---------- */
static void test_gmsg_basic(void)
{
    char group[MAX_GROUP_NAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_gmsg("/gmsg 学习小组 今天学什么",
                                group, sizeof(group), &msg);
    CHECK(r == 0, "gmsg 基本切分成功");
    CHECK(strcmp(group, "学习小组") == 0, "群名切出 学习小组");
    CHECK(msg != NULL && strcmp(msg, "今天学什么") == 0, "消息切出 今天学什么");
}

/* ---------- 用例 8：gmsg 多余空白 ---------- */
static void test_gmsg_extra_ws(void)
{
    char group[MAX_GROUP_NAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_gmsg("/gmsg   学习小组   今天学什么",
                                group, sizeof(group), &msg);
    CHECK(r == 0, "gmsg 多余空白仍成功");
    CHECK(strcmp(group, "学习小组") == 0, "群名切出（多空白）");
    CHECK(msg != NULL && strcmp(msg, "今天学什么") == 0, "消息跳过前导空白");
}

/* ---------- 用例 9：gmsg 消息内部空格保留 ---------- */
static void test_gmsg_msg_spaces(void)
{
    char group[MAX_GROUP_NAME_LEN];
    const char *msg = NULL;
    int r = commands_parse_gmsg("/gmsg 学习小组 今天 学 什么",
                                group, sizeof(group), &msg);
    CHECK(r == 0, "gmsg 内部空格切分成功");
    CHECK(msg != NULL && strcmp(msg, "今天 学 什么") == 0, "消息内部空格保留");
}

/* ---------- 用例 10：gmsg 缺参 ---------- */
static void test_gmsg_missing(void)
{
    char group[MAX_GROUP_NAME_LEN];
    const char *msg = NULL;
    CHECK(commands_parse_gmsg("/gmsg 学习小组", group, sizeof(group), &msg) == -1,
          "gmsg 缺消息返回 -1");
    CHECK(commands_parse_gmsg("/gmsg", group, sizeof(group), &msg) == -1,
          "gmsg 缺群名返回 -1");
    CHECK(commands_parse_gmsg("/gmsg   ", group, sizeof(group), &msg) == -1,
          "gmsg 只有空白返回 -1");
}

/* ---------- 用例 11：gmsg 群名超长拒绝 ---------- */
static void test_gmsg_group_too_long(void)
{
    char group[MAX_GROUP_NAME_LEN];
    const char *msg = NULL;
    char name[64];
    /* 32 字符群名超上限（MAX_GROUP_NAME_LEN-1=31） */
    memset(name, 'g', 32);
    name[32] = '\0';
    char input[256];
    snprintf(input, sizeof(input), "/gmsg %s hi", name);
    CHECK(commands_parse_gmsg(input, group, sizeof(group), &msg) == -1,
          "gmsg 超长群名返回 -1");
}

/* ---------- 用例 12：sendfile 黄金切分 ---------- */
static void test_sendfile_basic(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    int r = commands_parse_sendfile("/sendfile bob a.txt", target, sizeof(target),
                                    filename, sizeof(filename));
    CHECK(r == 0, "sendfile 基本切分成功");
    CHECK(strcmp(target, "bob") == 0, "目标切出 bob");
    CHECK(strcmp(filename, "a.txt") == 0, "文件名切出 a.txt");
}

/* ---------- 用例 13：sendfile 多余空白 ---------- */
static void test_sendfile_extra_ws(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    int r = commands_parse_sendfile("/sendfile   bob   a.txt", target, sizeof(target),
                                    filename, sizeof(filename));
    CHECK(r == 0, "sendfile 多余空白仍成功");
    CHECK(strcmp(target, "bob") == 0, "目标切出 bob（多空白）");
    CHECK(strcmp(filename, "a.txt") == 0, "文件名跳过前导空白");
}

/* ---------- 用例 14：sendfile 文件名含空格 ---------- */
static void test_sendfile_spaces(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    int r = commands_parse_sendfile("/sendfile bob my file.txt", target, sizeof(target),
                                    filename, sizeof(filename));
    CHECK(r == 0, "sendfile 含空格文件名成功");
    CHECK(strcmp(filename, "my file.txt") == 0, "文件名内部空格保留");
}

/* ---------- 用例 15：sendfile 缺参 ---------- */
static void test_sendfile_missing(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    CHECK(commands_parse_sendfile("/sendfile bob", target, sizeof(target),
                                  filename, sizeof(filename)) == -1,
          "缺文件名返回 -1");
    CHECK(commands_parse_sendfile("/sendfile", target, sizeof(target),
                                  filename, sizeof(filename)) == -1,
          "缺目标返回 -1");
    CHECK(commands_parse_sendfile("/sendfile   ", target, sizeof(target),
                                  filename, sizeof(filename)) == -1,
          "只有空白返回 -1");
}

/* ---------- 用例 16：sendfile 目标超长拒绝 ---------- */
static void test_sendfile_target_too_long(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    char t[64];
    memset(t, 'x', 32);  /* 32 字符超上限（MAX_USERNAME_LEN-1=31） */
    t[32] = '\0';
    char input[400];
    snprintf(input, sizeof(input), "/sendfile %s a.txt", t);
    CHECK(commands_parse_sendfile(input, target, sizeof(target),
                                  filename, sizeof(filename)) == -1,
          "超长目标返回 -1");
}

/* ---------- 用例 17：sendfile 文件名超长拒绝 ---------- */
static void test_sendfile_filename_too_long(void)
{
    char target[MAX_USERNAME_LEN];
    char filename[MAX_FILENAME_LEN];
    char f[300];
    memset(f, 'f', 256);  /* 256 字符超上限（MAX_FILENAME_LEN-1=255） */
    f[256] = '\0';
    char input[400];
    snprintf(input, sizeof(input), "/sendfile bob %s", f);
    CHECK(commands_parse_sendfile(input, target, sizeof(target),
                                  filename, sizeof(filename)) == -1,
          "超长文件名返回 -1");
}

/* ---------- 用例 18：tid 参数解析 ---------- */
static void test_tid_basic(void)
{
    uint32_t tid = 0;
    CHECK(commands_parse_tid_arg("5", &tid) == 0 && tid == 5, "tid '5' → 5");
    CHECK(commands_parse_tid_arg("  7", &tid) == 0 && tid == 7, "tid 前导空白 → 7");
    CHECK(commands_parse_tid_arg("0", &tid) == 0 && tid == 0, "tid '0' → 0");
}

/* ---------- 用例 19：tid 参数非法 ---------- */
static void test_tid_invalid(void)
{
    uint32_t tid = 0;
    CHECK(commands_parse_tid_arg("", &tid) == -1, "空 tid 返回 -1");
    CHECK(commands_parse_tid_arg("   ", &tid) == -1, "纯空白 tid 返回 -1");
    CHECK(commands_parse_tid_arg("abc", &tid) == -1, "字母 tid 返回 -1");
    CHECK(commands_parse_tid_arg("-1", &tid) == -1, "负号 tid 返回 -1");
    CHECK(commands_parse_tid_arg("12abc", &tid) == -1, "混合 tid 返回 -1");
    CHECK(commands_parse_tid_arg("4294967296", &tid) == -1, "超 uint32 tid 返回 -1");
}

int main(void)
{
    test_priv_basic();
    test_priv_extra_ws();
    test_priv_msg_spaces();
    test_priv_missing_msg();
    test_priv_missing_target();
    test_priv_target_too_long();
    test_gmsg_basic();
    test_gmsg_extra_ws();
    test_gmsg_msg_spaces();
    test_gmsg_missing();
    test_gmsg_group_too_long();
    test_sendfile_basic();
    test_sendfile_extra_ws();
    test_sendfile_spaces();
    test_sendfile_missing();
    test_sendfile_target_too_long();
    test_sendfile_filename_too_long();
    test_tid_basic();
    test_tid_invalid();

    if (failures > 0) {
        fprintf(stderr, "test_commands: %d 个用例失败\n", failures);
        return 1;
    }
    printf("test_commands: 全部通过\n");
    return 0;
}
