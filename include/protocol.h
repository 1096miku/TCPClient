#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "utils.h"

/* ==================== Message Types ==================== */

#define MSG_AUTH            0x01   /* C->S: username\0password */
#define MSG_CHAT            0x02   /* C->S: broadcast message to lobby */
#define MSG_PRIV            0x03   /* C->S: target\0message */
#define MSG_GCREATE         0x04   /* C->S: group_name */
#define MSG_GJOIN           0x05   /* C->S: group_name */
#define MSG_GLEAVE          0x06   /* C->S: group_name */
#define MSG_GMSG            0x07   /* C->S: group_name\0message */
#define MSG_USERS           0x08   /* C->S: request online user list */
#define MSG_FILE_INIT       0x09   /* C->S: target\0filename\0file_size_str */
#define MSG_FILE_ACCEPT     0x0A   /* C->S: transfer_id (4 bytes) */
#define MSG_FILE_REJECT     0x0B   /* C->S: transfer_id (4 bytes) */
#define MSG_FILE_CHUNK      0x0C   /* C->S: tid(4B)+offset(8B)+data */
#define MSG_FILE_COMPLETE   0x0D   /* C->S: transfer_id (4 bytes) */
#define MSG_FILE_CANCEL     0x0E   /* C->S: transfer_id (4 bytes) */
#define MSG_SERVER_MSG      0x0F   /* S->C: server announcement text */
#define MSG_ERROR           0x10   /* S->C: error_code(2B)\0error_message */
#define MSG_ONLINE_USERS    0x11   /* S->C: count(2B)\0user1\0user2\0... */

/* ==================== Error Codes ==================== */

#define ERR_NOT_AUTH        1     /* Not authenticated */
#define ERR_AUTH_FAILED     100   /* Invalid username or password length */
#define ERR_USER_TAKEN      101   /* Username already logged in */
#define ERR_BAD_CREDENTIALS 102   /* Invalid username or password */
#define ERR_NO_SUCH_USER    200   /* Target user not found */
#define ERR_NO_SUCH_GROUP   201   /* Group not found */
#define ERR_ALREADY_MEMBER  202   /* Already a member of the group */
#define ERR_GROUP_EXISTS    203   /* Group name already in use */
#define ERR_TRANSFER_STATE  300   /* Invalid transfer state */
#define ERR_UNKNOWN_MSG     99    /* Unknown message type */

/* ==================== API ==================== */

/**
 * @brief Build a complete protocol frame
 * @param msg_type   Message type byte
 * @param payload    Payload data (may be NULL if payload_len == 0)
 * @param payload_len Payload length in bytes
 * @param out        Output buffer
 * @param out_cap    Output buffer capacity (must be >= 5 + payload_len)
 * @return Total frame length on success, -1 if buffer too small
 */
int protocol_build_frame(uint8_t msg_type, const uint8_t *payload,
                         uint16_t payload_len, uint8_t *out, int out_cap);

/**
 * @brief Build a frame with a single null-terminated text payload
 */
int protocol_build_text1(uint8_t msg_type, const char *text,
                         uint8_t *out, int out_cap);

/**
 * @brief Build a frame with two null-terminated strings as payload
 *        Payload = part1\0part2
 */
int protocol_build_text2(uint8_t msg_type, const char *part1, const char *part2,
                         uint8_t *out, int out_cap);

/**
 * @brief Build an error response frame
 */
int protocol_build_error(uint16_t code, const char *msg,
                         uint8_t *out, int out_cap);

/**
 * @brief Build an online users list frame
 * @param count      Number of users
 * @param user_data  Pre-built payload: count(2B big-endian)\0user1\0user2...
 * @param data_len   Length of user_data
 */
int protocol_build_raw(uint8_t msg_type, const uint8_t *data, int data_len,
                       uint8_t *out, int out_cap);

/**
 * @brief Incrementally parse a byte stream for complete frames
 *
 * This function scans the data buffer for a complete protocol frame.
 * It does not maintain internal state — all state is in the data buffer.
 *
 * @param data         Input byte buffer
 * @param data_len     Number of bytes in buffer
 * @param msg_type     [out] Parsed message type
 * @param payload      [out] Pointer to payload within data (not a copy)
 * @param payload_len  [out] Length of payload
 * @return  >0: bytes consumed for one complete frame
 *           0: need more data (incomplete)
 *          -1: protocol error (magic mismatch or oversized payload)
 */
int protocol_parse(const uint8_t *data, int data_len,
                   uint8_t *msg_type, const uint8_t **payload,
                   uint16_t *payload_len);

#endif /* PROTOCOL_H */
