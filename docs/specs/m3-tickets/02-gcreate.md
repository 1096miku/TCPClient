# 02 — /gcreate 创建群

**What to build:** 用户输入 `/gcreate <群名>` 即可创建群组：客户端解析群名、构造创建请求帧并发送，服务器回发的成功公告（`Group '学习小组' created. You are the first member.`）或错误帧（重名/空名/超长）原样显示在终端，与其他服务器消息一致地不打断输入行。同时 `/help` 开始列出群组命令，术语表收录「群组」概念。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] `/gcreate 学习小组` 发送创建请求，收到 `Group '学习小组' created. You are the first member.` 公告
- [ ] 创建重名群收到 `错误(203): Group already exists`
- [ ] 空群名（`/gcreate`、`/gcreate  `）本地提示用法，不发送请求
- [ ] 超长群名（≥32 字符）本地拒绝；服务器侧空名/超长错误 `错误(201)` 原样显示
- [ ] `/help` 输出包含 /gcreate 及群组命令用法
- [ ] CONTEXT.md 术语表新增「群组」条目（含行为规则）
- [ ] e2e 冒烟：创建成功公告 + 重名错误场景通过
