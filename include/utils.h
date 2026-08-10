#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/* ==================== Logging ==================== */

#ifdef DEBUG
#define log_debug(fmt, ...) \
    utils_log("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define log_debug(fmt, ...) ((void)0)
#endif

#define log_info(fmt, ...) \
    utils_log("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) \
    utils_log("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void utils_log(const char *level, const char *file, int line,
               const char *fmt, ...);

/* ==================== Constants ==================== */

#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    64
#define MAX_GROUP_NAME_LEN  32
#define MAX_FILENAME_LEN    256
#define MAX_MESSAGE_LEN     65535

#define READ_BUF_SIZE       65536
#define WRITE_BUF_INIT      4096
#define WRITE_BUF_MAX       (256 * 1024)    /* 256 KB max per client */

#define PROTO_MAGIC         0xCAFE
#define PROTO_HEADER_SIZE   5
#define PROTO_MAX_PAYLOAD   (1024 * 1024)   /* 1 MB safety cap */

/* ==================== String Utilities ==================== */

/**
 * @brief Safe string copy, always null-terminates
 * @return length of src (not including null terminator)
 */
size_t utils_strncpy(char *dst, const char *src, size_t dst_size);

/**
 * @brief Strip trailing \r and \n from a string
 */
void utils_trim_newline(char *str);

/**
 * @brief Parse a decimal digit string range into an unsigned 64-bit value
 * @param start  Range start (inclusive)
 * @param end    Range end (exclusive)
 * @param out    [out] Parsed value
 * @return 0 success; -1 empty range, non-digit char, or overflow
 * @note Client-side superset (M4 file transfer reuses it for the tid
 *       argument, announcement tid, and INIT size) — intentional deviation
 *       from the server's utils.c, noted in the commit message.
 */
int utils_parse_u64_range(const char *start, const char *end, uint64_t *out);

/* ==================== Endianness Helpers ==================== */

static inline uint16_t utils_read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static inline void utils_write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static inline uint32_t utils_read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

static inline void utils_write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static inline uint64_t utils_read_u64_be(const uint8_t *buf)
{
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8)  |  (uint64_t)buf[7];
}

static inline void utils_write_u64_be(uint8_t *buf, uint64_t val)
{
    buf[0] = (uint8_t)(val >> 56);
    buf[1] = (uint8_t)(val >> 48);
    buf[2] = (uint8_t)(val >> 40);
    buf[3] = (uint8_t)(val >> 32);
    buf[4] = (uint8_t)(val >> 24);
    buf[5] = (uint8_t)(val >> 16);
    buf[6] = (uint8_t)(val >> 8);
    buf[7] = (uint8_t)(val);
}

#endif /* UTILS_H */
