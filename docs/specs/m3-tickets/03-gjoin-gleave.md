# 03 — /gjoin + /gleave 加入/离开群

**What to build:** 用户输入 `/gjoin <群名>` 加入他人创建的群（全员收到 `bob has joined group '学习小组'` 公告），输入 `/gleave <群名>` 离开群（自己收 `You have left group '学习小组'` 确认、剩余成员收离开公告）；最后一名成员离开时收到群已删除通知。不存在的群、重复加入、非成员操作均显示对应错误帧。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] `/gjoin 学习小组` 加入成功，自己与已有成员均收到加入公告
- [ ] 加入不存在的群收到 `错误(201): Group not found`
- [ ] 重复加入同一群收到 `错误(202): Already a member of this group`
- [ ] `/gleave 学习小组` 离开成功，自己收 `You have left group '学习小组'`，剩余成员收离开公告
- [ ] 最后一名成员离开时收到 `Group '学习小组' has been deleted (last member left)`
- [ ] 非成员离开（或加入空名/不存在群）收到 `错误(201)` 系列提示
- [ ] 空群名本地提示用法，不发送请求
- [ ] e2e 冒烟：加入/离开/删群/错误路径场景通过
