#include "protocol.h"

/**
 * @brief 构造一个完整协议帧（手写镜像服务器 src/protocol.c 的语义）
 *
 * 帧布局（全部大端）：
 *   [0..1] magic 0xCAFE  [2] 消息类型  [3..4] 载荷长度  [5..] 载荷
 */
int protocol_build_frame(uint8_t msg_type, const uint8_t *payload,
                         uint16_t payload_len, uint8_t *out, int out_cap)
{
    int total = PROTO_HEADER_SIZE + payload_len;
    if (total > out_cap) {
        return -1;
    }

    /* Magic number (big-endian) */
    utils_write_u16_be(out, PROTO_MAGIC);
    /* Message type */
    out[2] = msg_type;
    /* Payload length (big-endian) */
    utils_write_u16_be(out + 3, payload_len);
    /* Payload */
    if (payload_len > 0 && payload != NULL) {
        memcpy(out + PROTO_HEADER_SIZE, payload, payload_len);
    }
    return total;
}

/**
 * @brief 构造单文本帧：载荷 = text + 尾随 NUL（协议实际行为，与头文件注释一致）
 */
int protocol_build_text1(uint8_t msg_type, const char *text,
                         uint8_t *out, int out_cap)
{
    size_t text_len = strlen(text);
    if (text_len > PROTO_MAX_PAYLOAD) {
        return -1;
    }
    /* Include the null terminator in the payload */
    uint16_t payload_len = (uint16_t)(text_len + 1);
    return protocol_build_frame(msg_type, (const uint8_t *)text,
                                payload_len, out, out_cap);
}

/**
 * @brief 构造双文本帧：载荷 = part1\0part2\0（两个 NUL 都包含）
 *
 * 注意：此处直接手写帧头而非委托 build_frame，
 * 镜像服务器写法——学习点是"何时该复用、何时该内联"。
 */
int protocol_build_text2(uint8_t msg_type, const char *part1, const char *part2,
                         uint8_t *out, int out_cap)
{
    size_t len1 = strlen(part1);
    size_t len2 = strlen(part2);
    size_t total = len1 + 1 + len2 + 1;  /* part1\0part2\0 */

    if (total > PROTO_MAX_PAYLOAD) {
        return -1;
    }
    if ((int)total + PROTO_HEADER_SIZE > out_cap) {
        return -1;
    }

    /* Frame header */
    utils_write_u16_be(out, PROTO_MAGIC);
    out[2] = msg_type;
    utils_write_u16_be(out + 3, (uint16_t)(total));

    /* Payload: two null-terminated strings */
    uint8_t *payload = out + PROTO_HEADER_SIZE;
    memcpy(payload, part1, len1 + 1);               /* include \0 */
    memcpy(payload + len1 + 1, part2, len2 + 1);   /* include \0 */

    return (int)(total + PROTO_HEADER_SIZE);
}

/**
 * @brief 构造错误帧：载荷 = code(2B 大端)\0message\0，类型固定 MSG_ERROR
 */
int protocol_build_error(uint16_t code, const char *msg,
                         uint8_t *out, int out_cap)
{
    size_t msg_len = strlen(msg);
    size_t total = 2 + 1 + msg_len + 1;  /* code(2B)\0msg\0 */

    if (total > PROTO_MAX_PAYLOAD) {
        return -1;
    }
    if ((int)total + PROTO_HEADER_SIZE > out_cap) {
        return -1;
    }

    /* Frame header: error type is fixed */
    utils_write_u16_be(out, PROTO_MAGIC);
    out[2] = MSG_ERROR;
    utils_write_u16_be(out + 3, (uint16_t)(total));

    /* Payload: code(2B) \0 message \0 */
    uint8_t *payload = out + PROTO_HEADER_SIZE;
    utils_write_u16_be(payload, code);
    payload[2] = '\0';
    memcpy(payload + 3, msg, msg_len + 1);

    return (int)(total + PROTO_HEADER_SIZE);
}

/**
 * @brief 构造原始载荷帧：payload 原样透传
 */
int protocol_build_raw(uint8_t msg_type, const uint8_t *data, int data_len,
                       uint8_t *out, int out_cap)
{
    if (data_len > PROTO_MAX_PAYLOAD) {
        return -1;
    }
    return protocol_build_frame(msg_type, data, (uint16_t)data_len,
                                out, out_cap);
}

/**
 * @brief 增量解析字节流中的完整帧
 *
 * 无内部状态——所有状态都在数据缓冲里，剩余数据由调用方 memmove 归位。
 * 返回值约定：>0 一帧完整消耗的字节数；0 数据不够；-1 协议错误。
 *
 * 注意（镜像服务器的既有缺陷）：magic 不符时服务器会向后扫描寻找
 * 可能的 magic，但找到也一律返回 -1（注释声称会报告消耗字节数，
 * 实现却只有 -1）——客户端照抄该语义，与正确服务器通信不会触发。
 */
int protocol_parse(const uint8_t *data, int data_len,
                   uint8_t *msg_type, const uint8_t **payload,
                   uint16_t *payload_len)
{
    if (data_len < PROTO_HEADER_SIZE) {
        return 0;  /* need more data */
    }

    uint16_t magic = utils_read_u16_be(data);
    if (magic != PROTO_MAGIC) {
        /* Sync error: scan forward for a potential magic number */
        for (int i = 1; i < data_len - 1; i++) {
            if (utils_read_u16_be(data + i) == PROTO_MAGIC) {
                return -1;   /* 找到疑似 magic 也返回 -1，调用方丢弃整个缓冲 */
            }
        }
        return -1;  /* no magic found, discard entire buffer */
    }

    *msg_type = data[2];
    *payload_len = utils_read_u16_be(data + 3);

    int total_frame = PROTO_HEADER_SIZE + *payload_len;
    if (data_len < total_frame) {
        return 0;  /* need more data */
    }

    if (*payload_len > 0) {
        *payload = data + PROTO_HEADER_SIZE;
    } else {
        *payload = NULL;
    }
    return total_frame;
}
