#include "commands.h"

#include <stdio.h>
#include <string.h>

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
