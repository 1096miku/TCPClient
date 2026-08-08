#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "conn.h"
#include "utils.h"

/* 读缓冲初始大小：够放一个最大帧(5+65535) + 下一帧开头 */
#define APP_RBUF_INIT   (64 * 1024)
/* 读缓冲硬顶：超限视为协议错误断开 */
#define APP_RBUF_MAX    (256 * 1024)

/* 单行输入上限：最大消息(65535) + NUL */
#define APP_INPUT_MAX   (MAX_MESSAGE_LEN + 1)

/* 事件循环超时（ms）：SIGINT 标志最迟在这个间隔内被观察到 */
#define APP_POLL_TIMEOUT_MS  200

typedef struct app {
    conn_t *conn;          /* 拥有的连接 */
    uint8_t *rbuf;         /* 读缓冲（堆分配，realloc 扩容） */
    size_t   rbuf_cap;     /* 当前容量 */
    size_t   rbuf_len;     /* 有效字节数（分发后 memmove 归位） */
    bool     running;      /* 事件循环开关（/quit、EOF、SIGINT、断连均置 false） */
} app_t;

/**
 * @brief 连接服务器并完成登录握手
 * @param host 主机名或 IP
 * @param port 端口（1-65535，调用方已校验）
 * @return NULL 失败（连接失败/登录输入 EOF）；否则返回 app 句柄
 * @note 内部：conn_connect → 登录循环（提示输入、发送 MSG_AUTH、
 *       等待认证结果——服务器对失败不关连接，错误帧后同连接重试）
 */
app_t *app_connect(const char *host, uint16_t port);

/**
 * @brief 运行 poll 事件循环，直到退出条件触发
 * @return 0 正常退出；-1 异常（协议错误/连接异常）
 * @note 循环内：读 socket→增量解析→帧分发；读 stdin→命令解析；
 *       200ms 超时用于检查 g_running（SIGINT 标志）
 */
int app_run(app_t *app);

/**
 * @brief 释放连接与缓冲
 */
void app_destroy(app_t *app);

#endif /* APP_H */
