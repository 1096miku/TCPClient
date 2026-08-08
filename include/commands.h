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
 * @brief 处理一行用户输入
 * @param conn     已建立的连接
 * @param line     去除换行后的输入行（裸文本或 / 命令）
 * @param quit_out [out] 置 true 表示应退出（/quit）
 * @return 0 已处理；-1 致命错误（发送失败，应退出）
 * @note 裸文本 → MSG_CHAT 帧发送；/help /quit /login 命令分发；
 *       未知命令给出提示。M2+ 在此扩展 /priv /users /gcreate 等
 */
int commands_handle_line(conn_t *conn, const char *line, bool *quit_out);

#endif /* COMMANDS_H */
