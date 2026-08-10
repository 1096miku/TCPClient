# 01 — 测试缝与协议构造器（纯函数 + 组帧 + 单测注册）

**What to build:** M4 的测试缝与混凝土：命令解析纯函数（`/sendfile` 目标/文件名切分、`/accept` `/reject` `/cancel` 的 tid 数字校验）、文件域决策纯函数（公告分类与 tid 提取、INIT 帧三段载荷解析、文件名 basename 化、同名唯一化（exists 探针注入）、分片计划）、三个新帧构造器（INIT 三段 NUL、4B 大端 tid、tid+offset+data chunk）。全部以纯函数形式存在并经单元测试验证，是发送/接收两条链路的基础——其余票的决策全部依赖本票函数。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] `commands_parse_sendfile`：黄金切分（`/sendfile bob a.txt` → 目标=bob 文件名=a.txt）、多空白、文件名含空格（`/sendfile bob my file.txt` → 文件名=my file.txt）、缺目标/缺文件名返回失败、目标超长（≥32）拒绝、文件名超长（≥256）拒绝
- [ ] `commands_parse_tid_arg`：`"5"`→5、前导空白 `"  7"`→7、`"0"`→0、空/纯空白→-1、`"abc"`→-1、`"-1"`→-1、`"12abc"`→-1、超 uint32（`"4294967296"`）→-1
- [ ] `file_parse_announcement`：7 种公告文案（等待 accept/收到 incoming/已接受/已拒绝/完成/取消/断线取消）各一条黄金（含 `[Server] ` 前缀）→ 正确事件类型 + tid；无前缀版本可识别；普通公告（如 Welcome）→ 非文件事件；聊天文本含 "File transfer" 字样不误匹配；`File transfer #` 后无数字 → 非文件事件
- [ ] `file_parse_init_payload`：三段黄金 `alice\0a.txt\0100` → 发送方/文件名/大小；缺段、字段超长、大小非数字 → 失败
- [ ] `file_basename_sanitize`：`a/b/c.txt`→`c.txt`、`C:\dir\f.txt`→`f.txt`、空/`.`/`..` → 失败、控制字符替换为 `_`、超长 → 失败
- [ ] `file_unique_name`（假探针注入，不碰文件系统）：无冲突→原名、被占 1 次→`base (1)`、被占 3 次→`base (3)`、缓冲不足 → 失败
- [ ] `file_chunk_count/plan`：size=0→0 片；size=65500→1 片；size=65501→2 片（末片 1 字节）；index 越界 → 失败
- [ ] `protocol_build_text3`：MSG_FILE_INIT 载荷 `bob\0a.txt\0100` 黄金字节逐位一致 + 解析往返校验三段 NUL 位置
- [ ] `protocol_build_tid`：MSG_FILE_ACCEPT + tid=0x01020304 → `CA FE 0A 00 04 01 02 03 04`
- [ ] `protocol_build_chunk`：tid=1 offset=0 data="AB" 黄金字节（4B tid + 8B 零 offset + 数据）、data_len=0 边界、out_cap 不足 → -1
- [ ] 新增文件域测试目标注册（CMake：新目标 + add_test；命令测试目标追加文件模块依赖）；Release 构建 ctest 全绿（test_protocol / test_commands / 新目标全部用例）
