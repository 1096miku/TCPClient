#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

/* 提示符当前是否正显示在屏幕上 */
static bool prompt_shown = false;

/**
 * @brief 换行保护：若提示符正显示（用户可能正在输入半行），
 *        先输出换行把半行留在上方，再打印文本，最后重打提示符。
 *        服务器消息与错误帧都可能异步到达，必须走此保护。
 */
static void ui_emit_incoming(const char *text)
{
    if (prompt_shown) {
        printf("\n");          /* 半行输入留在上方，光标落到新行 */
        prompt_shown = false;  /* 复位，允许重打提示符 */
    }
    printf("%s\n", text);
    fflush(stdout);
    ui_prompt();               /* 重打提示符 */
}

void ui_print(const char *text)
{
    printf("%s\n", text);
    fflush(stdout);
}

void ui_display_incoming(const char *text)
{
    ui_emit_incoming(text);
}

void ui_print_error(uint16_t code, const char *message)
{
    char line[1024];
    snprintf(line, sizeof(line), "错误(%u): %s", code, message);
    ui_emit_incoming(line);
}

void ui_display_online_users(const uint8_t *payload, uint16_t plen)
{
    uint16_t count = utils_read_u16_be(payload);

    /* 载荷布局（chat.c:147-183）: count(2B) + '\0' + '\0' + user1\0user2\0...
     * 偏移 2 是 NUL 分隔符，偏移 3 是服务器构建时的预留残留，
     * 用户列表从偏移 4 开始 */
    char users[1024];
    size_t pos = 0;
    size_t i = 4;
    uint16_t seen = 0;
    while (i < plen && seen < count) {
        const char *name = (const char *)payload + i;
        size_t nlen = strlen(name);
        if (nlen == 0) {
            break;  /* 双 NUL 结束 */
        }
        if (pos + nlen + 2 < sizeof(users)) {
            if (seen > 0) {
                users[pos++] = ' ';
            }
            memcpy(users + pos, name, nlen);
            pos += nlen;
        }
        i += nlen + 1;
        seen++;
    }
    users[pos] = '\0';

    char line[1088];
    snprintf(line, sizeof(line), "在线用户(%u): %s", count, users);
    ui_emit_incoming(line);
}

void ui_prompt(void)
{
    if (prompt_shown) {
        return;
    }
    printf("> ");
    fflush(stdout);
    prompt_shown = true;
}

void ui_print_help(void)
{
    ui_print("可用命令:");
    ui_print("  /gcreate <群名>      创建群组");
    ui_print("  /gjoin <群名>        加入群组");
    ui_print("  /gleave <群名>       离开群组");
    ui_print("  /gmsg <群名> <消息>  群聊消息");
    ui_print("  /priv <用户> <消息>  发送私聊消息");
    ui_print("  /users              刷新在线用户列表");
    ui_print("  /login              重新登录（换用户名/密码）");
    ui_print("  /help               显示本帮助");
    ui_print("  /quit               退出客户端");
    ui_print("  其他输入            作为大厅聊天消息发送");
}
