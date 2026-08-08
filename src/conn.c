#include "conn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

/**
 * @brief 建立 TCP 连接
 *
 * 用 getaddrinfo 解析 host（支持主机名/IPv4/IPv6），
 * 遍历返回的所有地址逐个尝试 connect，第一个成功者胜出。
 * 这是"名字解析 → 逐个尝试"的经典模式。
 */
conn_t *conn_connect(const char *host, uint16_t port,
                     char *errbuf, size_t errsz)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* IPv4/IPv6 均可 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP 流 */

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0) {
        snprintf(errbuf, errsz, "%s", gai_strerror(gai));
        return NULL;
    }

    conn_t *c = NULL;
    int last_errno = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            last_errno = errno;
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            c = malloc(sizeof(conn_t));
            if (c) {
                c->fd = fd;
            } else {
                close(fd);
                last_errno = ENOMEM;
            }
            break;
        }
        last_errno = errno;
        close(fd);
    }
    freeaddrinfo(res);

    if (c == NULL) {
        snprintf(errbuf, errsz, "%s", strerror(last_errno));
    }
    return c;
}

/**
 * @brief 全量发送
 *
 * send 不保证一次发完（TCP 流的部分写），必须循环直到全部发出。
 * MSG_NOSIGNAL 防止对端关闭时触发 SIGPIPE 杀掉进程（与 SIGPIPE=SIG_IGN 双保险）。
 * EINTR（信号打断）时重试。
 */
int conn_send_all(conn_t *c, const uint8_t *data, int len)
{
    int sent = 0;
    while (sent < len) {
        ssize_t n = send(c->fd, data + sent, (size_t)(len - sent), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (int)n;
    }
    return 0;
}

int conn_get_fd(const conn_t *c)
{
    return c->fd;
}

void conn_close(conn_t *c)
{
    if (c == NULL) {
        return;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c);
}
