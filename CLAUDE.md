# 项目名称

## 项目概述

学习项目：通过编写聊天服务器（TCPServer，`d:\AAA_Game_XueXiBan\ShareUbuntu\CC\TCPServer`）的配套 C 客户端，学习 C 语言与 Claude Code 开发工作流。当前里程碑 M1：编解码 + 连接 + 登录 + 大厅聊天。规划中：M2 私聊 + 离线消息 + 在线列表；M3 群组；M4 文件传输；M5（可选）ncurses TUI。

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
├── tools/              # （规划中）e2e 驱动脚本
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
- [x] 检查点 A：ctest 全绿（test_protocol 14 用例）
- [x] 检查点 B：冒烟 e2e 通过（登录重试/Welcome/在线列表/广播回显/上线公告；Ctrl-C 与交互式提示符待手工三终端验证）
- 已定决策索引：单线程 poll 见 `docs/adr/0001`；领域术语见 `CONTEXT.md`
- 待办：git init + 首次 commit（用户触发）；M2 私聊 + 离线消息 + 在线列表命令

## 注意事项
- 构建/测试/运行一律在 WSL（`cd /mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient`）
- **构建默认用 Ninja 生成器**（CMakePresets.json，人和 Claude 统一走同一命令）：
  `cmake --preset wsl-release && cmake --build --preset wsl-release`（产物 `build-ninja/`，Release）；调试用 `--preset wsl-debug`（`build-ninja-debug/`，含 DEBUG 日志）；测试 `ctest --test-dir build-ninja --output-on-failure`。不要用 `cmake -B build-wsl` 默认生成器
- **不主动 git init / git commit / git push**，由用户触发；commit message 用简洁中文
- 协议以 TCPServer 为唯一权威来源（源码优先）；改协议先改服务器再改客户端
- include/utils.h、include/protocol.h、src/utils.c 与服务器保持逐字节一致（可 diff），改动需在提交说明中注明有意偏离
- 服务器对登录失败**不关连接**——错误帧后同一连接重试；广播回显含发送者本人（预期行为非 bug）
- **服务器已知 bug（2026-08-08 冒烟验证发现）**：`server_disconnect_client` 广播 "has left the chat" 时经 `client_queue_send` 写入对方 write_buf 但**不写 notify_fd 唤醒主线程**（唤醒只发生在 `server_dispatch_message` 的 cleanup 段），边沿触发下公告永久滞留——对方收不到 has left 公告。客户端行为正确（服务器没发就无法显示），修复在服务器侧（disconnect 广播后 `write(s->notify_fd, &val, sizeof(val))` 或直接 `server_flush_all_writes`）。未修复前 e2e 场景⑥无法通过
- 新会话首条消息需给出计划文件路径（`C:\Users\1\.claude\plans\tcp-c-users-1-claude-plans-cc-1-lvgl9-2-cozy-moler.md`）要求先读再动手
- 会话结束前实时更新"当前开发状态"
