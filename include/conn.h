#ifndef CONN_H
#define CONN_H

#include <stdint.h>
#include <stddef.h>

/* 连接句柄：封装 socket fd。单线程，无需锁。 */
typedef struct conn {
    int fd;              /* -1 表示已关闭 */
} conn_t;

/**
 * @brief 建立 TCP 连接
 * @param host   主机名或 IP（getaddrinfo 解析，遍历所有结果）
 * @param port   端口（1-65535，调用方已校验）
 * @param errbuf 错误信息输出缓冲（getaddrinfo/connect 失败原因）
 * @param errsz  errbuf 大小
 * @return 新连接（堆分配），失败返回 NULL
 * @note 失败信息用 gai_strerror/strerror 写入 errbuf
 */
conn_t *conn_connect(const char *host, uint16_t port,
                     char *errbuf, size_t errsz);

/**
 * @brief 全量发送（部分写循环 + MSG_NOSIGNAL，send 被 EINTR 打断时重试）
 * @return 0 成功；-1 失败（连接已断开，调用方应结束循环）
 */
int conn_send_all(conn_t *c, const uint8_t *data, int len);

/**
 * @brief 取 fd 供 poll 使用
 */
int conn_get_fd(const conn_t *c);

/**
 * @brief 关闭连接并释放
 */
void conn_close(conn_t *c);

#endif /* CONN_H */
