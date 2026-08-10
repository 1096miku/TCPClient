#ifndef COMMANDS_H
#define COMMANDS_H

#include "conn.h"
#include <stdbool.h>

/**
 * @brief 提示输入用户名/密码并发送 MSG_AUTH 帧
 * @param conn 已建立的连接
 * @return 0 发送成功；-1 输入 EOF（Ctrl-D）/发送失败
 * @note 用户名 ≤ MAX_USERNAME_LEN-1、密码 ≤ MAX_PASSWORD_LEN-1，超长重输；
 *       认证结果（Welcome/错误帧）由调用方在事件循环中等待；
 *       M1 不回显关闭（termios ECHO off 留到 M5 ncurses 阶段）
 */
int commands_login(conn_t *conn);

/**
 * @brief 切分 /priv 命令参数：target 与 message
 * @param line      输入行（调用方已确认以 /priv 开头）
 * @param target    [out] 目标用户名（NUL 结尾）
 * @param target_sz target 缓冲区大小
 * @param msg_out   [out] 指向消息文本（跳过 target 与中间空白，内部空格保留）
 * @return 0 成功；-1 格式错误（缺 target/缺消息/target 超长）
 * @note target 长度上限 target_sz-1；message 为空视为缺消息；
 *       纯函数（无 I/O），独立可测
 */
int commands_parse_priv(const char *line, char *target, size_t target_sz,
                        const char **msg_out);

/**
 * @brief 切分 /gmsg 命令参数：group 与 message（与 parse_priv 同构）
 * @param line      输入行（调用方已确认以 /gmsg 开头）
 * @param group     [out] 群组名（NUL 结尾）
 * @param group_sz  group 缓冲区大小
 * @param msg_out   [out] 指向消息文本（跳过群名与中间空白，内部空格保留）
 * @return 0 成功；-1 格式错误（缺群名/缺消息/群名超长）
 * @note 群名长度上限 group_sz-1（服务器以 ≥MAX_GROUP_NAME_LEN 拒绝）；
 *       纯函数（无 I/O），独立可测
 */
int commands_parse_gmsg(const char *line, char *group, size_t group_sz,
                        const char **msg_out);

/**
 * @brief 处理一行用户输入
 * @param conn     已建立的连接
 * @param line     去除换行后的输入行（裸文本或 / 命令）
 * @param quit_out [out] 置 true 表示应退出（/quit）
 * @return 0 已处理；-1 致命错误（发送失败，应退出）
 * @note 裸文本 → MSG_CHAT 帧发送；/help /quit /login /priv /users 及
 *       群组命令分发；未知命令给出提示。M4+ 在此扩展 /sendfile 等
 */
int commands_handle_line(conn_t *conn, const char *line, bool *quit_out);

#endif /* COMMANDS_H */
