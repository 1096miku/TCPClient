# 04 — M4 收尾：e2e 自动断言 + 文档 + 全场景回归

**What to build:** 文件传输里程碑的完整交付：自动断言 e2e 脚本跑通全场景（真实服务器 + 多客户端，wait_for 轮询替代固定 sleep，接收文件与源文件 diff 字节校验，FAIL 计数 + 非零退出码）；项目文档（里程碑状态）更新到 M4 完成；Release/Debug 双预设构建 + ctest 全绿确认无回归。

**Blocked by:** 02 — 发送链路；03 — 接收链路

**Status:** ready-for-agent

- [ ] e2e 脚本：WSL 内运行；服务器端口 18084（避开残留进程）；客户端从 `/tmp/e2e_m4/cwd` 启动（downloads/ 不落仓库）；日志写 `/tmp/e2e_m4/`；wait_for 轮询 grep 替代固定 sleep
- [ ] 场景① happy path：64KB 随机文件 alice→bob，接收文件与源文件 `diff -q` 一致；双方日志断言 accepted 与 complete
- [ ] 场景② reject：bob 拒绝后 alice 见 `rejected by` 公告，downloads/ 无残留
- [ ] 场景③ cancel：8MB 发送中 alice `/cancel`，bob 侧无半成品残留
- [ ] 场景④ 目标不在线：alice `/sendfile carol` → `错误(200)` 且槽位释放（后续场景的发送能成功）
- [ ] 场景⑤ 非法 tid：bob `/accept 999` → 本地拒绝提示"没有 #999 的传输记录"
- [ ] 场景⑥ 同名加序号：第二次发送同名文件 → `name (1)` 存在且 diff 通过
- [ ] 场景⑦ 多 pending：alice 连续发两个文件 → bob 逐个 accept，两个文件均 diff 通过
- [ ] 全部场景 PASS 汇总、退出码 0（任一 FAIL 非零退出）
- [ ] CLAUDE.md「当前开发状态」更新：M4 完成、检查点、待办指向 M5
- [ ] Release 与 Debug 预设构建 + ctest 全绿（test_protocol / test_commands / 文件域测试全部用例）
