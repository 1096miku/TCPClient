# Handoff — TCPClient M4（票 01/02/03 已交付提交，下一步票 04 收尾）

> 生成时间：2026-08-10。新会话交接文档——从 **票 04（M4 收尾：e2e 自动断言 + 文档 + 全场景回归）** 继续。

## Context

学习项目：为聊天服务器（TCPServer，`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`，零改动）编写配套 C 客户端学 C 语言与 Claude Code 工作流。本会话完成 M4 票 03（接收链路）+ 两轴 code-review 修复闭环，**票 01/02/03 均已提交**（用户触发提交）。

- 仓库：`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPClient`（分支 main）
  - HEAD = `68d7d80`（票 03 接收链路）；前序：`84d4664`（票 01/02 发送链路）、`a389842`（M3）
- 服务器：`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`（完整存在，零改动，只读参照）

## 本会话完成（勿重复实现，文档引用即可）

| 项 | 位置 |
|---|---|
| 票 03 接收状态机（pending 任务/INIT seq 填充/accept 落盘/CHUNK fseek/COMPLETE 校验/取消路径/单槽规则） | `src/file.c`、`include/file.h` |
| app 分发接线（MSG_FILE_INIT/CHUNK/COMPLETE case、tick 节流）+ /accept /reject 命令 | `src/app.c`、`src/commands.c` |
| 纯函数 +4 用例（span_check/mark）→ test_file 41 用例全绿 | `tests/test_file.c` |
| 全 C 双端冒烟（16 项断言，两轮全绿、服务器零丢包） | `tools/m4_smoke_t03.sh` |
| 实测修复记录（忙循环塌缩、INIT 帧交错）与决策修正 | `docs/specs/M4-文件传输.md`（Further Notes）、`docs/adr/0002-文件传输状态机.md` |
| 术语同步（分片：乱序合法按 offset 写盘） | `CONTEXT.md` |
| CLAUDE.md「当前开发状态」已更新至票 03 完成——**此修改未提交**，票 04 提交时带上 | `CLAUDE.md` |
| .gitignore 增加 `downloads/` `*.log`（运行产物防误提交） | `.gitignore`（已随票 03 提交） |

## 重要事实（新会话必读）

1. **poll 忙循环塌缩（已修，2026-08-10 实测）**：原 tick 在每轮事件处理后执行，接收流连续到达时（自环传输）poll 永不超时，轮循环塌缩成 ~1ms 忙循环，发送速率失控（strace 实测 4MB 全部片 47ms 发出），服务器 write_buf 256KB 超限静默丢包（16MB 丢 122 片）。修复：**tick 仅在 poll 超时轮（r==0）执行**（app.c），事件轮跳过；每轮片数 4→2（`FILE_CHUNKS_PER_ROUND=2`，≈128KB 留余量）。修复后 16MB/32MB 零丢包。**这是发送节奏的硬约束，票 04 改发送逻辑前必读 spec Further Notes**。
2. **INIT 帧关联跨传输交错（已修）**：服务器线程池并行处理不同传输的 INIT，公告/INIT 帧对可能跨传输交错（charlie 的 INIT 帧被填入 bob 的任务）。修复：接收任务记录公告到达序号（seq），INIT 帧匹配 seq 最小未填充者（file.c `recv_handle_init`）。残余限制：pending 满自动 REJECT 后到达的孤儿 INIT 帧仍可能误填——概率低，注释已承认。
3. **服务器写缓冲静默丢包**（client_queue_send，WRITE_BUF_MAX=256KB）：客户端限流对策 = 2 片/轮 + 超时轮节流。诊断手段：服务器日志 grep "exceeded max"；strace 看 sendto 节奏（tools/ 下调试脚本已删除，复现方法见 spec Further Notes）。
4. **e2e/冒烟现状**：`tools/m4_smoke_t03.sh`（WSL 内运行，日志/工作目录走 `/tmp/m4_smoke/`）——run A alice 自环（happy path/同名序号/非法 tid）+ run B bob/charlie→alice（reject/单槽/重复 accept/cancel 删半成品/发送槽忙/公告双信号）。票 04 的 `tools/e2e_m4.sh` 应在其基础上增强（wait_for 轮询替代固定 sleep、场景⑦多 pending、端口 18084）。
5. **测试账号**（服务器 `data/passwd.txt`）：alice:password123、bob:password456、charlie:password789。
6. **协议层偏离**：protocol.c（text3/tid/chunk）与 utils.c（parse_u64_range）为客户端超集，票 01/02 提交说明已注明"有意偏离"（CLAUDE.md 规则，后续提交无需重复）。
7. **服务器已知 bug 不影响本里程碑**：disconnect 广播不写 notify_fd（公告滞留）——e2e 断言不依赖断线公告，用 CANCEL 帧/主动取消覆盖取消路径。
8. **运行残留清理**：客户端从仓库根运行会生成 downloads/ 与 .log——已被 .gitignore 覆盖；规范做法是从 /tmp 工作目录运行（票 04 e2e 已约定 `/tmp/e2e_m4/cwd`）。

## 下一步：票 04 收尾（docs/specs/m4-tickets/04-m4-wrap-up.md）

- 验收 11 条：`tools/e2e_m4.sh` 自动断言 e2e（真实服务器 + 多客户端、wait_for 轮询 grep 替代固定 sleep、diff 字节校验、FAIL 计数 + 非零退出码）场景①-⑦；CLAUDE.md「当前开发状态」更新到 M4 完成、待办指向 M5；Release/Debug 双预设构建 + ctest 全绿全回归
- 已就位的基础：`tools/m4_smoke_t03.sh`（场景②③⑤⑥⑦部分已覆盖，可对照/抽取）、spec Testing Decisions 的 e2e 场景清单（`docs/specs/M4-文件传输.md`）
- 参照：M3 的 `tools/e2e_m3.sh`（wait_for 轮询先例）

## 关键参照文件

| 文件 | 用途 |
|---|---|
| `docs/specs/m4-tickets/04-m4-wrap-up.md` | 票 04 验收清单（11 条，权威） |
| `docs/specs/M4-文件传输.md` | spec 全文（决策 + 怪癖清单 + Further Notes 实测记录） |
| `tools/m4_smoke_t03.sh` | 票 03 冒烟（16 断言，e2e_m4.sh 的起点） |
| `src/file.c` / `include/file.h` | 传输状态机（发送 2 片/轮 + 接收单槽 + seq 关联） |
| `src/app.c` | poll 循环（tick 仅超时轮执行——勿回退） |
| `CLAUDE.md` | 项目规范 + 当前开发状态（票 04 更新 + 本会话收尾改动待提交） |
| `TCPServer/src/file_transfer.c` | 服务器协议权威 |

## 验证命令（WSL 内）

```bash
cd /mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient
cmake --preset wsl-release && cmake --build --preset wsl-release
ctest --test-dir build-ninja --output-on-failure   # test_protocol / test_commands / test_file 全绿
bash tools/m4_smoke_t03.sh                          # 票 03 冒烟 16 项（回归用）
```

## 工作流注意事项

- 构建/测试/运行一律在 WSL；`wsl bash -lc '...'` 有引号/路径转换问题（heredoc 变量会被外层展开）——复杂脚本写文件再执行
- **pkill 自杀陷阱**：内联命令含 `chat_server <port>` 字符串时，pkill -f 会杀掉当前 bash——启动/停止服务器一律走脚本文件
- 不自动 git commit/push；commit message 简洁中文；提交前展示变更摘要
- 服务器零改动；协议事实以服务器源码为唯一权威
- 每票独立窗口（用户定下的流程）：票 04 完成后停下问用户，再 /handoff 或 /clear
- 票 04 完成时建议 /code-review（Standards + Spec 两轴），固定点取 HEAD
- **handoff 文档放本项目 `docs/handoff/`（用户要求，2026-08-10，覆盖 skill 默认 temp 目录）**

## Suggested skills（票 04 建议调用）

- **mattpocock-skills:implement**：逐票实施（e2e 脚本 + 文档更新 + 回归）
- **mattpocock-skills:code-review**：票 04 收尾两轴审查（Spec 轴会逐条勾验 04 票 11 条验收）
- **mattpocock-skills:tdd**：若 e2e 脚本需要新增可测纯函数（如 wait_for 辅助），先写失败测试
- **mattpocock-skills:handoff**：票 04 完成后写交接文档（本文件同款结构），供 M5 规划窗口使用
