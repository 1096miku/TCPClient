#ifndef UI_H
#define UI_H

#include <stdint.h>

/**
 * @brief 打印一行客户端本地文本（帮助、退出信息等），原样输出
 * @note 与 ui_display_incoming 的区别：调用时机由本地流程控制，
 *       不处理"消息打断半行输入"的提示符问题
 */
void ui_print(const char *text);

/**
 * @brief 展示一条服务器下发消息（公告/聊天广播/错误文本）
 * @note 服务器消息可能在任何时刻到达（包括用户打半行时）：
 *       若提示符正显示，先换行把半行输入留在上方，打印消息后重打提示符
 */
void ui_display_incoming(const char *text);

/**
 * @brief 打印错误帧，格式：错误(102): Invalid username or password
 * @note 与 ui_display_incoming 相同的换行保护（错误帧同样异步到达）
 */
void ui_print_error(uint16_t code, const char *message);

/**
 * @brief 显示在线用户列表，格式：在线用户(N): user1 user2 ...
 * @param payload MSG_ONLINE_USERS 载荷：count(2B 大端)\0user1\0user2\0...
 * @param plen    载荷长度
 */
void ui_display_online_users(const uint8_t *payload, uint16_t plen);

/**
 * @brief 输出提示符 "> "（幂等：已显示则不再重复输出）
 */
void ui_prompt(void);

/**
 * @brief 打印 /help 帮助文本
 */
void ui_print_help(void);

#endif /* UI_H */
