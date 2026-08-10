# 03 — 接收链路（incoming → /accept 落盘 → 校验完成）

**What to build:** 接收方端到端可用：incoming 公告建任务（tid 从公告解析）与 INIT 帧字段填充；`/accept <tid>` 落盘 downloads/（自动建目录、basename 化、同名加序号、打开失败提示）；CHUNK 按 offset 连续性校验后写盘（不连续/越界 → 主动取消 + 删半成品）；COMPLETE 字节数校验（不符删除 + 提示）；`/reject` 与接收方 `/cancel`；pending 上限与活动接收互斥规则。帮助文本含 /accept /reject 说明。

**Blocked by:** 01 — 测试缝与协议构造器

**Status:** ready-for-agent

- [ ] incoming 公告到达 → 建 pending 任务（tid 从 `Incoming file transfer #N from ...` 解析）并显示公告；INIT 帧到达填充发送方/文件名/大小（公告为主、INIT 帧为补充）
- [ ] `/accept <tid>` → downloads/ 目录自动创建；文件名 basename 化；同名自动加序号（`name`、`name (1)`...）；发送接受帧；本地提示保存路径；文件打开失败提示
- [ ] `/reject <tid>` → 发送拒绝帧 + 任务清理；对未知 tid → 本地提示"没有 #N 的传输记录"不发送
- [ ] CHUNK 到达：tid 匹配活动任务且 offset 不重复、不越界 → **fseek 定位写入**（服务器多线程转发可能乱序，见 spec Further Notes——乱序到达正常，按 offset 落盘）；重复/越界 offset → 发送取消帧 + 删除半成品 + 提示
- [ ] COMPLETE 帧或完成公告到达（幂等）→ 关闭文件；已收字节数==声明大小 → 保留文件 + 提示保存路径；不符 → 删除 + 提示不完整
- [ ] 接收方 `/cancel <tid>` → 发送取消帧 + 删除半成品 + 任务清理
- [ ] 收到取消帧/取消公告/断线公告 → 删除半成品 + 任务清理（接收方视角）
- [ ] pending 满 8 时新 incoming 自动回发拒绝帧并提示；已有活动接收时第二个 `/accept` 本地拒绝并提示
- [ ] 帮助文本已含 `/accept <tid> 接受文件` 与 `/reject <tid> 拒绝文件`
