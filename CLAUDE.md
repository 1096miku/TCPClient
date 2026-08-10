# 项目名称

## 项目概述

学习项目：通过编写聊天服务器（TCPServer，`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`）的配套 C 客户端，学习 C 语言与 Claude Code 开发工作流。当前里程碑 M4：文件传输（/sendfile /accept /reject /cancel 已交付；M1 编解码+连接+登录+大厅聊天、M2 私聊+离线消息+在线列表命令、M3 群组已完成）。规划中：M5（可选）ncurses TUI。

## 技术栈

- C11、CMake（≥3.10）、CTest
- 单线程 poll() 事件循环（socket fd + STDIN_FILENO，200ms 超时），不用 pthread
- **构建/测试/运行一律在 WSL**（Windows 无 poll/unistd/MSG_NOSIGNAL 无法编译）

## 项目结构
```
TCPClient/
├── CMakeLists.txt / .gitignore / README.md / CLAUDE.md / CONTEXT.md
├── .github/workflows/ci.yml
├── include/            # utils.h(复制) protocol.h(复制) conn.h app.h commands.h ui.h
├── src/                # 同名 .c 实现（protocol.c 手写镜像，utils.c 复制）
├── tests/test_protocol.c
├── tools/              # e2e 冒烟脚本（WSL 内运行，日志写 /tmp 不落仓库）
└── docs/adr/           # 中文 ADR
```

## 编码规范

- C11 + `-Wall -Wextra -pedantic`，产物输出到 `bin/`
- snake_case 命名；函数注释用 Doxygen 风格
- **注释语言**：复制文件（utils.h / protocol.h / utils.c）保持英文不动；新建文件用中文注释 + Doxygen 函数头
- 新增模块（M2-M4）时同步维护 CONTEXT.md 术语表

## 当前开发状态

- [x] M0 仓库骨架（CLAUDE.md / CONTEXT.md / ADR-0001 / CI）
- [x] M1 编解码 + 连接 + 登录 + 大厅聊天（完成）
- [x] M2 私聊 + 离线消息 + 在线列表命令（完成：`/priv` `/users`、MSG_PRIV 分发、离线回放）
- [x] M3 群组（完成：`/gcreate` `/gjoin` `/gleave` `/gmsg`、MSG_GMSG 分发、帮助与术语表）
- [x] M4 文件传输（完成：`/sendfile` `/accept` `/reject` `/cancel`、MSG_FILE_* 帧族分发、轮询分片 + 超时轮 tick 节流、fseek 乱序写盘、COMPLETE 字节校验、单槽规则、公告驱动 tid 解析）
- [x] 检查点 A：ctest 全绿（test_protocol 14 + test_commands 11 + test_file 41 用例）
- [x] 检查点 B：M1/M2/M3 冒烟通过；M4 e2e 全场景通过（`tools/e2e_m4.sh`，26 项断言：happy path diff 一致/reject 无残留/cancel 删半成品/目标不在线错误 200+槽位释放/非法 tid 本地提示/同名加序号/multi 发送方多 pending 逐个 accept；服务器零丢包）
- 已定决策索引：单线程 poll 见 `docs/adr/0001`；文件传输状态机见 `docs/adr/0002`；领域术语见 `CONTEXT.md`；M4 规格与 4 票见 `docs/specs/`
- 待办：M5（可选）ncurses TUI 规划；交互式提示符/Ctrl-C 手工三终端验证（M3 遗留——冒烟均为管道喂入，未在真实终端验证）

## 注意事项
- 构建/测试/运行一律在 WSL（`cd /mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient`）
- **构建默认用 Ninja 生成器**（CMakePresets.json，人和 Claude 统一走同一命令）：
  `cmake --preset wsl-release && cmake --build --preset wsl-release`（产物 `build-ninja/`，Release）；调试用 `--preset wsl-debug`（`build-ninja-debug/`，含 DEBUG 日志）；测试 `ctest --test-dir build-ninja --output-on-failure`。不要用 `cmake -B build-wsl` 默认生成器
- **不主动 git init / git commit / git push**，由用户触发；commit message 用简洁中文
- 协议以 TCPServer 为唯一权威来源（源码优先）；改协议先改服务器再改客户端
- include/utils.h、include/protocol.h、src/utils.c 与服务器保持逐字节一致（可 diff），改动需在提交说明中注明有意偏离
- 服务器对登录失败**不关连接**——错误帧后同一连接重试；广播回显含发送者本人（预期行为非 bug）
- **服务器已知 bug（2026-08-08 冒烟验证发现，未修）**：`server_disconnect_client` 广播 "has left the chat" 时经 `client_queue_send` 写入对方 write_buf 但**不写 notify_fd 唤醒主线程**（唤醒只发生在 `server_dispatch_message` 的 cleanup 段），边沿触发下公告永久滞留——对方收不到 has left 公告。客户端行为正确（服务器没发就无法显示），修复在服务器侧（disconnect 广播后 `write(s->notify_fd, &val, sizeof(val))` 或直接 `server_flush_all_writes`）。未修复前 e2e 场景⑥无法通过
- **客户端已修 bug（2026-08-08 M2 冒烟发现）**：登录成功帧（Welcome）与后续帧（离线公告/回放/在线列表/上线公告）同批 recv 粘连到达时，login_loop 消费 Welcome 后 break，剩余帧留在内存 rbuf——主循环 poll 只监听 socket fd，socket 已空则 POLLIN 永不触发，剩余帧被吞。修复：login_loop 成功后立即 `app_dispatch(app)` 分发 rbuf 剩余帧（app.c 登录段）。M1 冒烟时该 bug 被 TCP 分片掩盖（Welcome 与在线列表恰好分批到达）
- **客户端已修 bug（2026-08-10 M4 票 03 冒烟发现）**：发送 tick 原在每轮事件处理后执行，接收流连续到达时（自环传输）poll 永不超时，轮循环塌缩成 ~1ms 忙循环，发送速率失控（4MB 全部片 47ms 发出）触发服务器写缓冲超限静默丢包。修复：tick 仅在 poll 超时轮（r==0）执行（app.c）；每轮片数 4→2（≈128KB 留余量）。同次发现并修：INIT 帧关联跨传输交错（服务器线程池并行，公告/INIT 帧对可能交错）——接收任务加公告到达序号（seq）FIFO 匹配（file.c）。详见 spec Further Notes
- 新会话首条消息需给出计划文件路径（`C:\Users\1\.claude\plans\tcp-c-users-1-claude-plans-cc-1-lvgl9-2-cozy-moler.md`）要求先读再动手
- 会话结束前实时更新"当前开发状态"
