#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "conn.h"
#include "utils.h"  /* MAX_USERNAME_LEN / MAX_FILENAME_LEN */

/* ==================== 常量 ==================== */

#define FILE_CHUNK_DATA_MAX       65500  /* 单片数据上限（帧长 5+12+65500=65517 ≤ 服务器读缓冲约束） */
#define FILE_CHUNKS_PER_ROUND     4      /* 每轮最大发送片数（≈256KB ≤ 服务器写缓冲 256KB） */
#define FILE_MAX_RECV_PENDING     8      /* 接收 pending 任务上限 */
#define FILE_PROGRESS_INTERVAL    (1024u * 1024u)  /* 发送进度打印水位（1 MiB） */
#define FILE_SEND_POLL_TIMEOUT_MS 50     /* 发送期间 poll 超时（毫秒） */
#define FILE_DOWNLOAD_DIR         "downloads"

/* ==================== 公告事件类型 ==================== */

typedef enum {
    FILE_ANN_NONE = 0,         /* 非文件传输公告 */
    FILE_ANN_SEND_PENDING,     /* File transfer #N: ... Waiting for acceptance... */
    FILE_ANN_RECV_PENDING,     /* Incoming file transfer #N from ... */
    FILE_ANN_ACCEPTED,         /* File transfer #N accepted by ... */
    FILE_ANN_REJECTED,         /* File transfer #N rejected by ... */
    FILE_ANN_COMPLETE,         /* File transfer #N complete: ... */
    FILE_ANN_CANCELLED,        /* File transfer #N cancelled by ... */
    FILE_ANN_CANCELLED_DISC    /* File transfer #N cancelled: ... disconnected（无帧） */
} file_ann_kind_t;

/* ==================== 发送任务 ==================== */

typedef enum {
    FILE_SEND_NONE = 0,
    FILE_SEND_PENDING_TID,   /* INIT 已发，等待自己的公告解析 tid */
    FILE_SEND_PENDING,       /* tid 已知，等待对方 accept */
    FILE_SEND_ACTIVE,        /* 已 accept，轮询发片中 */
    FILE_SEND_DONE,          /* 全部片已发，下一轮 tick 发 COMPLETE（规避服务器竞态） */
    FILE_SEND_WAIT_COMPLETE  /* COMPLETE 帧已发，等 complete 公告 */
} file_send_state_t;

typedef struct file_send_task {
    bool     busy;
    uint32_t tid;                /* 公告解析所得；0 表示尚未关联 */
    char     target[MAX_USERNAME_LEN];
    char     filename[MAX_FILENAME_LEN];
    uint64_t size;
    uint64_t sent;               /* 已发字节数（= 下一片 offset） */
    uint64_t next_progress_mark; /* 进度打印水位 */
    FILE    *fp;                 /* 仅 ACTIVE 时非 NULL */
    file_send_state_t state;
} file_send_task_t;

/* ==================== API ==================== */

/**
 * @brief /sendfile 命令入口：解析参数→校验→发 INIT 帧→登记发送任务
 * @return 0 已处理（含本地拒绝提示）；-1 发送失败（应退出）
 */
int file_cmd_sendfile(conn_t *conn, const char *line);

/**
 * @brief /cancel 命令入口：取消匹配的传输（发送任务；接收任务见票 03）
 * @return 0 已处理（含本地提示）；-1 发送失败（应退出）
 */
int file_cmd_cancel(conn_t *conn, const char *line);

/**
 * @brief 公告事件入口：解析文件传输公告并驱动状态迁移（公告为主信号）
 * @note 由 app 在公告打印之后调用，只做状态迁移不重复显示
 */
void file_handle_announcement(const char *text);

/**
 * @brief 帧分发入口：处理文件传输控制帧（帧为冗余信号，与公告幂等）
 * @note 票 02 处理发送方视角（ACCEPT/REJECT/CANCEL）；INIT/CHUNK/COMPLETE 票 03
 */
void file_handle_frame(conn_t *conn, uint8_t type,
                       const uint8_t *payload, uint16_t plen);

/**
 * @brief 错误帧挂钩：清理匹配的发送任务
 * @note 200 仅在 PENDING_TID 清理（INIT 目标不在线）；
 *       300 只来自文件传输操作，任何发送状态均清理
 */
void file_handle_error(uint16_t code);

/**
 * @brief 是否有活动发送任务（poll 超时选择依据）
 */
bool file_send_active(void);

/**
 * @brief 轮询发送：每轮最多 FILE_CHUNKS_PER_ROUND 片，发完自动发 COMPLETE
 * @return 0 正常；-1 发送失败（连接已断，应退出事件循环）
 */
int file_send_tick(conn_t *conn);

/**
 * @brief 重置全部传输状态（连接建立/销毁时调用）：关句柄、清任务
 */
void file_reset_all(void);

/* ==================== API（纯函数，独立可测） ==================== */

/**
 * @brief 解析服务器公告文本：分类文件传输事件并提取传输标识
 * @param text      MSG_SERVER_MSG 载荷原文（可能含 "[Server] " 前缀，可无）
 * @param kind_out  [out] 事件类型（非文件公告置 FILE_ANN_NONE）
 * @param tid_out   [out] 传输标识（仅返回 0 时有效）
 * @return 0 是文件传输公告（kind/tid 有效）；-1 非文件公告或解析失败
 * @note 识别以 "File transfer #" / "Incoming file transfer #" 开头的文本，
 *       数字后须为 ':' 或 ' '；tid 须 ≤ UINT32_MAX（服务器全局单调递增分配）
 */
int file_parse_announcement(const char *text, file_ann_kind_t *kind_out,
                            uint32_t *tid_out);

/**
 * @brief 解析 INIT 帧载荷：sender\0filename\0size_str（三段 NUL 结尾）
 * @param payload    载荷
 * @param plen       载荷长度
 * @param sender     [out] 发送方用户名
 * @param sender_sz  sender 缓冲区大小
 * @param filename   [out] 文件名
 * @param filename_sz filename 缓冲区大小
 * @param size_out   [out] 文件大小（十进制全数字，拒绝空白/符号/混合）
 * @return 0 成功；-1 任一字段缺失/超长/大小非法
 */
int file_parse_init_payload(const uint8_t *payload, uint16_t plen,
                            char *sender, size_t sender_sz,
                            char *filename, size_t filename_sz,
                            uint64_t *size_out);

/**
 * @brief 文件名 basename 化 + 控制字符清理（防目录穿越，服务器不校验文件名）
 * @param raw    原始文件名（可含路径分隔符 / 与 \）
 * @param out    [out] 清理后的文件名（取最后一个分隔符之后）
 * @param out_sz out 缓冲区大小
 * @return 0 成功；-1 空/"."/".."/超长
 * @note 控制字符（< 0x20）替换为 '_'
 */
int file_basename_sanitize(const char *raw, char *out, size_t out_sz);

/**
 * @brief 路径存在性探测（access 封装）
 * @return true 存在
 */
bool file_path_exists(const char *path);

/**
 * @brief 同名唯一化：base、base (1)、base (2)... 首个不冲突者
 * @param base    基础文件名
 * @param exists  存在性探针（生产传 file_path_exists 的包装，测试传假探针）
 * @param ctx     探针上下文
 * @param out     [out] 唯一化结果
 * @param out_sz  out 缓冲区大小
 * @return 0 成功；-1 缓冲不足或试满 1000 次
 */
int file_unique_name(const char *base,
                     bool (*exists)(const char *name, void *ctx), void *ctx,
                     char *out, size_t out_sz);

/**
 * @brief 目录与文件名拼接：dir/name
 * @return 0 成功；-1 结果超长
 */
int file_join_path(const char *dir, const char *name, char *out, size_t out_sz);

/**
 * @brief 分片总数：size=0 → 0；否则 ceil(size / chunk_size)
 */
uint64_t file_chunk_count(uint64_t size, uint32_t chunk_size);

/**
 * @brief 第 index 片的分片计划
 * @param size        文件大小
 * @param chunk_size  单片大小（非零）
 * @param index       片序号（0 起）
 * @param offset_out  [out] 该片起始偏移
 * @param len_out     [out] 该片字节数（末片可能小于 chunk_size）
 * @return true 成功；false index 越界
 */
bool file_chunk_plan(uint64_t size, uint32_t chunk_size, uint64_t index,
                     uint64_t *offset_out, uint32_t *len_out);

#endif /* FILE_H */
