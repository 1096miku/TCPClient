# TCP Chat Client (tcp_chat_client)

基于单线程 poll() 事件循环的纯 C 语言 TCP 聊天客户端（学习项目），配套聊天服务器 [TCPServer](https://github.com/1096miku/TCPServer)。

> 学习项目：核心目的是理解 poll、TCP 流协议、单线程事件循环。协议与服务器完全兼容，服务器零改动。

## 功能特性

- 登录（用户名 + 密码，失败同一连接重试）
- 大厅聊天（广播 + 自身回显）
- 在线用户列表显示（登录自动 + /users 手动刷新）
- 私聊（/priv，含发送方回声、错误帧提示）
- 离线消息（目标离线时缓存，登录后自动回放）
- 群组（/gcreate 创建 /gjoin 加入 /gleave 离开 /gmsg 群聊）
- /help /quit /login 命令

**M4+ 规划**：文件传输（/sendfile /accept /reject）、（可选）ncurses TUI。

## 架构概览

```
main ──> app(poll 事件循环) ──> conn(连接/发送)
   │        │  ├─ protocol(帧编解码)     ui(输出层，可换 ncurses)
   │        │  └─ commands(命令/裸文本)  utils(工具)
   └────────┴───────── 单线程，无锁
```

## 快速开始

依赖：Linux/WSL、CMake ≥ 3.10、C11 编译器。

```bash
cmake --preset wsl-release            # 配置（Ninja + Release，产物目录 build-ninja/）
cmake --build --preset wsl-release    # 构建
```

调试构建（含 DEBUG 日志）：`cmake --preset wsl-debug && cmake --build --preset wsl-debug`（`build-ninja-debug/`）。

产物：`bin/tcp_client`。

## 运行

```bash
./bin/tcp_client <host> [port]
# port 可选，默认 18080
```

示例（服务器运行在 `./bin/chat_server 18080 data/passwd.txt`）：

```bash
./bin/tcp_client 127.0.0.1 18080
```

## 通信协议

大端二进制帧：`Magic(2B 0xCAFE) + type(1B) + len(2B) + payload`，详见 `include/protocol.h`。共 17 种消息类型（0x01-0x0E C→S，0x0F-0x11 S→C），完整定义与 TCPServer 一致。

## 测试

```bash
ctest --test-dir build-ninja --output-on-failure   # 编解码 + 命令解析单元测试
```

手工端到端（WSL 三终端）：终端 1 起服务器，终端 2/3 起客户端，验证登录、大厅广播、私聊、离线回放、优雅退出（见 `docs/adr/` 与 CONTEXT.md）。

## 项目结构

```
include/  头文件（utils.h/protocol.h 与服务器逐字节一致）
src/      实现（protocol.c 手写镜像；utils.c 复制）
tests/    ctest 单元测试
tools/    e2e 冒烟脚本（WSL 内运行）
docs/     ADR 决策记录 + specs/ 里程碑规格与 ticket
```

## 文档

- `claude.md` 开发文档与硬规则
- `CONTEXT.md` 领域词汇表（共享语言）
- `docs/adr/` 架构决策记录

## 许可

学习用途。
