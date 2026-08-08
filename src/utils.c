#include "utils.h"
#include <stdarg.h>

void utils_log(const char *level, const char *file, int line,
               const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = localtime_r(&now, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(stderr, "[%s] [%s] %s:%d: ",
            time_str, level, file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
}

size_t utils_strncpy(char *dst, const char *src, size_t dst_size)
{
    if (dst_size == 0) return 0;

    size_t i;
    for (i = 0; i < dst_size - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';

    /* Return full length of src */
    return strlen(src);
}

void utils_trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}
