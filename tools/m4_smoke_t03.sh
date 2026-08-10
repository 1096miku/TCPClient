#!/bin/bash
# 票 03 冒烟 e2e（全 C 双端）：接收方为 C 客户端（此前为 Python recv.py）
# 运行：WSL 内 bash tools/m4_smoke_t03.sh；日志/工作目录走 /tmp/m4_smoke/（不落仓库）
# run A：alice 自环（发送+接收同机）：happy path + 同名加序号 + 非法 tid
# run B：bob/charlie→alice：reject + 发送槽忙 + 第二个 accept 本地拒绝 + cancel 删半成品
set -u
SRV=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPServer
CLI=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient
D=/tmp/m4_smoke
P=0
fail() { echo "FAIL: $1"; P=1; }
pass() { echo "PASS: $1"; }

# ---------- run A：alice 自环 ----------
A=$D/runA
rm -rf $A && mkdir -p $A && cd $A
head -c 16777216 /dev/urandom > $A/src1.bin   # 16MB
pkill -f "chat_server 18086" 2>/dev/null; sleep 0.5
(cd $SRV && exec ./bin/chat_server 18086 data/passwd.txt) >$A/server.log 2>&1 &
SRV_PID=$!
sleep 1

# alice：登录 → /sendfile alice src1.bin(#1) → /accept 1 → /accept 99(无记录)
#        → /sendfile alice src1.bin(#2 同名) → /accept 2 → /quit
( sleep 1; printf "alice\npassword123\n"
  sleep 2.5; echo "/sendfile alice $A/src1.bin"
  sleep 2;   echo "/accept 1"
  sleep 2;   echo "/accept 99"
  sleep 5;   echo "/sendfile alice $A/src1.bin"
  sleep 2;   echo "/accept 2"
  sleep 10;  echo "/quit"; sleep 1 ) \
  | $CLI/bin/tcp_client 127.0.0.1 18086 >$A/alice.log 2>&1

kill $SRV_PID 2>/dev/null; pkill -f "chat_server 18086" 2>/dev/null; sleep 0.5

echo "===== run A：alice 自环 ====="
cat $A/alice.log
grep -c "exceeded max" $A/server.log | xargs -I{} echo "服务器丢包日志计数: {}"
if cmp -s $A/src1.bin "$A/downloads/src1.bin"; then
    pass "A: 传输1 字节一致 (downloads/src1.bin)"
else
    fail "A: 传输1 字节不一致"
fi
if cmp -s $A/src1.bin "$A/downloads/src1.bin (1)"; then
    pass "A: 同名加序号 (downloads/src1.bin (1)) 字节一致"
else
    fail "A: 同名序号文件缺失或字节不一致"
fi
grep -q "没有 #99 的传输记录。" $A/alice.log && pass "A: 非法 tid 本地提示" \
    || fail "A: 非法 tid 提示缺失"
grep -q "文件 #1 接收完成，已保存: downloads/src1.bin" $A/alice.log \
    && pass "A: #1 完成校验保留" || fail "A: #1 完成提示缺失"
grep -q "文件 #2 接收完成，已保存: downloads/src1.bin (1)" $A/alice.log \
    && pass "A: #2 完成校验保留" || fail "A: #2 完成提示缺失"

# ---------- run B：bob/charlie→alice ----------
# tid 分配取决于服务器线程池处理顺序：charlie 先提交（t≈10.5）→ #2
# （服务器实测不保证跨传输顺序，脚本按"先提交先分配"编排）
B=$D/runB
rm -rf $B && mkdir -p $B && cd $B
head -c 16777216 /dev/urandom > $B/src_a.bin   # 16MB（#1，bob→alice happy path）
head -c 8388608  /dev/urandom > $B/src_b.bin   # 8MB（#3，bob 发，reject 用）
head -c 8388608  /dev/urandom > $B/src_c.bin   # 8MB（#2，charlie 发，单槽拒绝窗口）
head -c 33554432 /dev/urandom > $B/src_d.bin   # 32MB（#4，bob 发，cancel 用）
pkill -f "chat_server 18087" 2>/dev/null; sleep 0.5
(cd $SRV && exec ./bin/chat_server 18087 data/passwd.txt) >$B/server.log 2>&1 &
SRV_PID=$!
sleep 1

# alice（接收方，t≈0 启动）：登录 → /accept 1(t≈7.5) → /accept 2(t≈16)
#   → 重复 /accept 2(该传输已在接收中) → /accept 3(单槽拒绝) → /reject 3
#   → /accept 99(非法) → /accept 4(t≈23, 32MB) → /cancel 4(删半成品) → /quit
( sleep 1; printf "alice\npassword123\n"
  sleep 6.5; echo "/accept 1"
  sleep 8.5; echo "/accept 2"
  sleep 0.3; echo "/accept 2"
  sleep 0.3; echo "/accept 3"
  sleep 2.5; echo "/reject 3"
  sleep 1.5; echo "/accept 99"
  sleep 2.5; echo "/accept 4"
  sleep 3;   echo "/cancel 4"
  sleep 2;   echo "/quit"; sleep 1 ) \
  | $CLI/bin/tcp_client 127.0.0.1 18087 >$B/alice.log 2>&1 &
A_PID=$!

sleep 2
# bob（发送方，t≈2 启动）：登录 → 发#1(t≈6) → 等 complete → 发#3(t≈15.5)
#   + 连发(槽忙) → 等 rejected 公告 → 发#4(t≈21.8) → /quit
( sleep 1; printf "bob\npassword456\n"
  sleep 2.5; echo "/sendfile alice $B/src_a.bin"
  sleep 9.5; echo "/sendfile alice $B/src_b.bin"
  sleep 0.3; echo "/sendfile alice $B/src_b.bin"
  sleep 6;   echo "/sendfile alice $B/src_d.bin"
  sleep 7;   echo "/quit"; sleep 1 ) \
  | $CLI/bin/tcp_client 127.0.0.1 18087 >$B/bob.log 2>&1 &
B_PID=$!

sleep 2.5
# charlie（发送方，t≈2.5 启动）：登录 → 发#2(t≈10.5) → /quit
( sleep 1; printf "charlie\npassword789\n"
  sleep 3.5; echo "/sendfile alice $B/src_c.bin"
  sleep 18;  echo "/quit"; sleep 1 ) \
  | $CLI/bin/tcp_client 127.0.0.1 18087 >$B/charlie.log 2>&1 &
C_PID=$!

wait $A_PID; wait $B_PID; wait $C_PID
kill $SRV_PID 2>/dev/null; pkill -f "chat_server 18087" 2>/dev/null; sleep 0.5

echo "===== run B：bob/charlie→alice ====="
echo "--- alice.log ---"
cat $B/alice.log
echo "--- bob.log ---"
cat $B/bob.log
echo "--- charlie.log ---"
cat $B/charlie.log
if cmp -s $B/src_a.bin "$B/downloads/src_a.bin"; then
    pass "B: #1 字节一致"
else
    fail "B: #1 字节不一致"
fi
if cmp -s $B/src_c.bin "$B/downloads/src_c.bin"; then
    pass "B: #2 (charlie) 字节一致"
else
    fail "B: #2 字节不一致"
fi
ls "$B/downloads/" | grep -q src_b && fail "B: reject 的 #3 有残留" \
    || pass "B: reject 后无 #3 残留"
ls "$B/downloads/" | grep -q src_d && fail "B: cancel 的 #4 半成品未删除" \
    || pass "B: cancel 后半成品已删除"
grep -q "已有进行中的接收任务" $B/alice.log && pass "B: 第二个 /accept 本地拒绝" \
    || fail "B: 单槽拒绝提示缺失"
grep -q "该传输已在接收中" $B/alice.log && pass "B: 重复 accept 防御" \
    || fail "B: 重复 accept 提示缺失"
grep -q "已拒绝 #3。" $B/alice.log && pass "B: /reject 处理" \
    || fail "B: /reject 提示缺失"
grep -q "已取消 #4。" $B/alice.log && pass "B: /cancel 处理" \
    || fail "B: /cancel 提示缺失"
grep -q "已有进行中的发送任务" $B/bob.log && pass "B: 发送槽忙本地拒绝" \
    || fail "B: 发送槽忙提示缺失"
grep -q "File transfer #3 rejected by alice" $B/bob.log && pass "B: bob 收 reject 公告" \
    || fail "B: bob 未收 reject 公告"
grep -q "File transfer #2 complete" $B/charlie.log && pass "B: charlie 收 complete 公告" \
    || fail "B: charlie 未收 complete 公告"
grep -q "File transfer #4 cancelled by alice" $B/bob.log && pass "B: bob 收 cancel 公告" \
    || fail "B: bob 未收 cancel 公告"

exit $P
