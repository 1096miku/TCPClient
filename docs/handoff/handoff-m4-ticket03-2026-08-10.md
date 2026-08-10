# Handoff — TCPClient M4（票 01/02 已交付，下一步票 03 接收链路）

> 生成时间：2026-08-10。新会话交接文档——从 **票 03（接收链路）** 继续 M4 文件传输开发。

## Context

学习项目：为聊天服务器（TCPServer，`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`，零改动）编写配套 C 客户端学 C 语言与 Claude Code 工作流。本会话完成 M4 的 grill → to-spec → to-tickets → 票 01（测试缝与协议构造器）→ 票 02（发送链路）+ 两轴 code-review 修复闭环。**所有 M4 工作未提交**（git 由用户触发）。

- 仓库：`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPClient`（分支 main，HEAD = M3 提交 `a389842`）
- 服务器：`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`（完整存在，零改动，只读参照）

## 本会话完成（勿重复实现，文档引用即可）

| 项 | 位置 |
|---|---|
| CONTEXT.md 术语 +4（文件传输/传输标识/分片/接收目录）、ADR-0002（单槽+轮询分片+公告驱动+落盘安全） | `CONTEXT.md`、`docs/adr/0002-文件传输状态机.md` |
| M4 spec（22 用户故事 + 实现/测试决策 + 怪癖清单） | `docs/specs/M4-文件传输.md` |
| 4 张垂直切片票（01 缝/02 发送/03 接收/04 收尾） | `docs/specs/m4-tickets/` |
| 票 01：纯函数 8 个 + 组帧 3 个 + test_file 新目标（ctest 3/3 全绿） | `include/file.h`、`src/file.c`、`tests/test_file.c`、`src/protocol.c`（text3/tid/chunk）、`src/commands.c`（parse_sendfile/parse_tid_arg）、`src/utils.c`（utils_parse_u64_range） |
| 票 02：发送状态机 + 轮询分片 + app 接线 + code-review 修复（COMPLETE 清理 bug、重复代码抽取、注释统一、app.c 还原重放） | `src/file.c`、`src/app.c`、`src/ui.c`（help +4 行） |

## 重要事实（新会话必读）

1. **服务器 chunk 乱序中继（实测，spec 已记录）**：服务器线程池并行处理同连接帧，chunk 转发顺序可能与发送顺序不一致（实测相邻块交换/循环错位）。**接收方必须按 offset fseek 定位写盘**，乱序到达是正常现象；重复/越界 offset 才视为错误。票 03 验收清单已据此更新。
2. **发满即发 COMPLETE 的竞态（实测，已规避）**：同一 tick 发完末片立即发 COMPLETE 会被服务器线程池竞态丢弃末片（实测 3144000/3145728）。客户端规避：发满后置 `FILE_SEND_DONE`，**下一轮 tick（≥50ms）才发 COMPLETE 帧**。规避后 4 轮冒烟 6 次传输全部字节一致。已知局限：竞态若仍发生，接收方字节数校验兜底（删除+提示），发送方无法感知。
3. **tid 只存在于公告文本**（协议事实）：INIT 帧载荷无 tid；公告先于对应控制帧到达；断线取消只有文本公告无 CANCEL 帧；对已结束 tid 的 CANCEL 静默忽略。
4. **e2e/冒烟工具**：`/tmp/m4_smoke/run.sh` + `recv.py`（WSL 内，临时脚本不落仓库）已验证票 02 发送链路——**票 03 完成后接收方换成 C 客户端即可全 C 双端验证**；脚本要点：`(cd $SRV && exec ...) &` 启动服务器（$! 才是 chat_server PID）、pkill 兜底清残留、recv.py 按 offset 写盘。
5. **测试账号**（服务器 `data/passwd.txt`）：alice:password123、bob:password456、charlie:password789。
6. **协议层偏离**：protocol.c（text3/tid/chunk）与 utils.c（parse_u64_range）为客户端超集，注释已注明有意偏离——git commit 说明需注明（CLAUDE.md 规则）。

## 下一步：票 03 接收链路（docs/specs/m4-tickets/03-receive-link.md）

- 扩展点现状：app.c 分发 switch 已接 `MSG_FILE_ACCEPT/REJECT/CANCEL`（发送方视角）；**接收方视角的 `MSG_FILE_INIT/CHUNK/COMPLETE` 仍落 default 分支**（打印"M4 扩展"）——票 03 补这三个 case + `/accept` `/reject` 命令分支（/cancel 分支已存在，file_cmd_cancel 需加接收任务匹配）
- 关键实现约束（spec Implementation Decisions + 票验收）：
  - 接收状态机：incoming 公告建 pending（tid 从公告解析）→ INIT 帧填充 sender/filename/size（公告为主、帧为补充）→ `/accept` 落盘 downloads/（自动建目录、basename 化 + 唯一化探针）→ CHUNK **按 offset fseek 写盘**（乱序正常，重复/越界才取消）→ COMPLETE 字节数校验（不符删除+提示）
  - 单槽规则：pending ≤8（满则自动回发 REJECT）、活动接收 ≤1（第二个 /accept 本地拒绝）；发送与接收槽独立
  - 取消/断线路径：CANCEL 帧/取消公告/断线公告/`/cancel` → 删半成品 + 清理；错误帧(300) 匹配接收任务也清理
- 已就位的纯函数（票 01，直接复用）：`file_parse_announcement`（RECV_PENDING 分类）、`file_parse_init_payload`、`file_basename_sanitize`、`file_unique_name`（探针注入）、`file_join_path`、`file_chunk_plan`、`file_path_exists`
- 收尾（票 04 范畴，勿提前做）：e2e_m4.sh 自动断言、CLAUDE.md/README 更新、全回归

## 关键参照文件

| 文件 | 用途 |
|---|---|
| `docs/specs/M4-文件传输.md` | spec 全文（决策 + 怪癖清单，权威） |
| `docs/specs/m4-tickets/03-receive-link.md` | 票 03 验收清单（9 条，含 fseek 写盘更新） |
| `docs/adr/0002-文件传输状态机.md` | 架构决策（单槽/轮询/公告驱动） |
| `src/file.c` / `include/file.h` | 发送状态机先例（结构/风格/幂等双信号模式），接收任务结构待加 |
| `src/app.c` | 分发 switch 扩展点（MSG_ERROR 挂钩、FILE 帧 case） |
| `src/commands.c` | 命令分支（/sendfile /cancel 已接，/accept /reject 待接） |
| `TCPServer/src/file_transfer.c` | 服务器协议权威（状态机/公告文案） |
| `CONTEXT.md` | 术语表（文件传输/传输标识/分片/接收目录） |

## 验证命令（WSL 内）

```bash
cd /mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient
cmake --preset wsl-release && cmake --build --preset wsl-release
ctest --test-dir build-ninja --output-on-failure   # test_protocol / test_commands / test_file 全绿
bash /tmp/m4_smoke/run.sh                          # 冒烟（目前 Python 作接收方；票 03 后改全 C）
```

## 工作流注意事项

- 构建/测试/运行一律在 WSL；`wsl bash -lc '...'` 有引号/路径转换问题（heredoc 变量会被外层展开）——复杂脚本写文件再执行
- 不自动 git commit/push；commit message 简洁中文；提交说明注明 protocol.c/utils.c 有意偏离
- 服务器零改动；协议事实以服务器源码为唯一权威
- 每票独立窗口（用户定下的流程）：票 03 完成后停下问用户，再 /handoff 或 /clear 开票 04 窗口
- 票 03 完成时建议 /code-review（Standards + Spec 两轴），固定点仍取 HEAD 或上一票
- **handoff 文档放本项目 `docs/handoff/`（用户要求，2026-08-10）**

## Suggested skills（票 03 建议调用）

- **mattpocock-skills:implement**：逐票实施，TDD 于既有缝（纯函数已就位，状态机与 I/O 交织部分按 spec 不单测、e2e 兜底）
- **mattpocock-skills:tdd**：新增纯函数（若有）先写失败测试
- **mattpocock-skills:code-review**：票 03 收尾两轴审查（Spec 轴会逐条勾验 03 票 9 条验收）
- **mattpocock-skills:handoff**：票 03 完成后写交接文档（本文件同款结构），供票 04 窗口使用
