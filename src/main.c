#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "app.h"
#include "utils.h"

/* SIGINT/SIGTERM 置 0；poll 200ms 超时观察到后优雅退出。
 * app.c 以 extern 引用（镜像服务器 main.c 定义 / server.c 引用的关系） */
volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[])
{
    /* 用法: tcp_client <host> [port]，port 默认 18080 */
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "用法: %s <host> [port]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *host = argv[1];

    long port = 18080;
    if (argc == 3) {
        char *end = NULL;
        errno = 0;
        port = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' ||
            port < 1 || port > 65535) {
            fprintf(stderr, "无效端口: %s（1-65535）\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    /* SIGINT（Ctrl-C）→ g_running=0（graceful）；SIGPIPE 忽略
     * （send 侧另有 MSG_NOSIGNAL 双保险，防对端断开时进程被杀） */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    app_t *app = app_connect(host, (uint16_t)port);
    if (app == NULL) {
        return EXIT_FAILURE;
    }

    int rc = app_run(app);
    app_destroy(app);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
