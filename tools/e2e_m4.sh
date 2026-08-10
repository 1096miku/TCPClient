#!/bin/bash
# M4 收尾 e2e：文件传输全场景自动断言（真实服务器 + 多客户端）
# 运行：WSL 内 bash tools/e2e_m4.sh；日志/工作目录走 /tmp/e2e_m4/（不落仓库）
# 场景：① happy path 64KB alice→bob（diff 字节一致 + accepted/complete 公告）
#       ② reject（无残留） ③ cancel（8MB 发送中取消，无半成品）
#       ④ 目标不在线（错误 200 + 槽位释放） ⑤ 非法 tid（本地提示）
#       ⑥ 同名加序号（f.bin 与 "f.bin (1)" 均 diff 通过） ⑦ 多 pending 逐个 accept
# 设计：每场景独立服务器实例（tid 恒从 1 起）+ 独立 cwd；客户端 stdin 走 FIFO，
#       命令由 wait_for 轮询驱动（替代固定 sleep 编排）；任一 FAIL → 退出码 1
set -u

SRV=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPServer
CLI=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient
PORT=18084
D=/tmp/e2e_m4
PASS=0
FAIL=0

[ -x "$SRV/bin/chat_server" ] || { echo "服务器二进制缺失，先在 TCPServer 构建"; exit 2; }
[ -x "$CLI/bin/tcp_client" ] || { echo "客户端二进制缺失，先 cmake --preset wsl-release 构建"; exit 2; }

rm -rf "$D"
mkdir -p "$D"
trap 'kill $SRV_PID 2>/dev/null' EXIT

pass() { echo "PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL + 1)); }

# wait_for <日志文件> <grep 模式> [超时秒=20]：0.2s 轮询，超时打印日志尾部
wait_for() {
    local log=$1 pat=$2 t=${3:-20} i=0
    while [ $i -lt $((t * 5)) ]; do
        [ -f "$log" ] && grep -q "$pat" "$log" && return 0
        sleep 0.2
        i=$((i + 1))
    done
    echo "    !! wait_for 超时(${t}s): '$pat' 未出现在 $log"
    [ -f "$log" ] && tail -8 "$log" | sed 's/^/    | /'
    return 1
}

# wait_gone <路径> [超时秒=10]：轮询直到文件不存在（半成品删除断言）
wait_gone() {
    local f=$1 t=${2:-10} i=0
    while [ $i -lt $((t * 5)) ]; do
        [ ! -e "$f" ] && return 0
        sleep 0.2
        i=$((i + 1))
    done
    echo "    !! wait_gone 超时(${t}s): $f 仍存在"
    return 1
}

# start_srv <场景名>：独立服务器实例（tid 恒从 1 起）
start_srv() {
    (cd "$SRV" && exec ./bin/chat_server $PORT data/passwd.txt) >"$D/$1-server.log" 2>&1 &
    SRV_PID=$!
    sleep 1
}
stop_srv() {
    kill $SRV_PID 2>/dev/null
    wait $SRV_PID 2>/dev/null
}

# client_up <场景名> <名字>：FIFO 喂 stdin 启动客户端（工作目录=场景 cwd），
# 输出 PID。注意两点（实测踩坑）：
#  1. 命令替换在子 shell 执行，fd 打开须由调用者在父 shell 完成；
#  2. 后台进程若继承命令替换的 stdout，命令替换要等其退出（等 EOF）——
#     整个后台子 shell 必须重定向 stdout/stderr 到 /dev/null。
client_up() {
    local sc=$1 name=$2
    mkfifo "$D/$sc-$name.in"
    ( cd "$D/$sc" && cat "$D/$sc-$name.in" | "$CLI/bin/tcp_client" 127.0.0.1 $PORT \
        >"$D/$sc-$name.log" 2>&1 ) >/dev/null 2>&1 &
    echo $!
}

# login <fd> <user> <pass> <日志>：喂凭据并等 Welcome
login() {
    echo "$2" >&$1
    echo "$3" >&$1
    wait_for "$4" "Welcome" >/dev/null || fail "$4 登录未完成"
}

# ===== 场景① happy path：64KB alice→bob，diff 字节一致 =====
S1=$D/s1
mkdir -p "$S1"
head -c 65536 /dev/urandom > "$D/f1.bin"
start_srv s1
A_PID=$(client_up s1 alice)
exec 3>$D/s1-alice.in
B_PID=$(client_up s1 bob)
exec 4>$D/s1-bob.in
login 3 alice password123 "$S1-alice.log"
login 4 bob password456 "$S1-bob.log"
echo "/sendfile bob $D/f1.bin" >&3
if wait_for "$S1-bob.log" "Incoming file transfer #1 from alice"; then
    echo "/accept 1" >&4
else
    fail "S1: bob 未收到 incoming 公告"
fi
wait_for "$S1-alice.log" "File transfer #1 accepted by bob" \
    && pass "S1: alice 见 accepted 公告" || fail "S1: alice 未见 accepted 公告"
wait_for "$S1-alice.log" "File transfer #1 complete" \
    && pass "S1: alice 见 complete 公告" || fail "S1: alice 未见 complete 公告"
wait_for "$S1-bob.log" "文件 #1 接收完成，已保存: downloads/f1.bin" \
    && pass "S1: bob 完成落盘提示" || fail "S1: bob 完成提示缺失"
diff -q "$D/f1.bin" "$S1/downloads/f1.bin" \
    && pass "S1: diff 字节一致" || fail "S1: 字节不一致"
echo "/quit" >&3
echo "/quit" >&4
wait $A_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&-

# ===== 场景② reject：bob 拒绝，alice 见公告，downloads/ 无残留 =====
S2=$D/s2
mkdir -p "$S2"
head -c 65536 /dev/urandom > "$D/f2.bin"
start_srv s2
A_PID=$(client_up s2 alice)
exec 3>$D/s2-alice.in
B_PID=$(client_up s2 bob)
exec 4>$D/s2-bob.in
login 3 alice password123 "$S2-alice.log"
login 4 bob password456 "$S2-bob.log"
echo "/sendfile bob $D/f2.bin" >&3
if wait_for "$S2-bob.log" "Incoming file transfer #1"; then
    echo "/reject 1" >&4
else
    fail "S2: bob 未收到 incoming 公告"
fi
wait_for "$S2-alice.log" "File transfer #1 rejected by bob" \
    && pass "S2: alice 见 rejected 公告" || fail "S2: alice 未见 rejected 公告"
wait_for "$S2-bob.log" "已拒绝 #1。" \
    && pass "S2: bob 本地提示" || fail "S2: bob 本地提示缺失"
wait_gone "$S2/downloads/f2.bin" \
    && pass "S2: downloads/ 无残留" || fail "S2: downloads/ 有残留"
echo "/quit" >&3
echo "/quit" >&4
wait $A_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&-

# ===== 场景③ cancel：8MB 发送中 alice /cancel，bob 无半成品 =====
S3=$D/s3
mkdir -p "$S3"
head -c 8388608 /dev/urandom > "$D/f3.bin"
start_srv s3
A_PID=$(client_up s3 alice)
exec 3>$D/s3-alice.in
B_PID=$(client_up s3 bob)
exec 4>$D/s3-bob.in
login 3 alice password123 "$S3-alice.log"
login 4 bob password456 "$S3-bob.log"
echo "/sendfile bob $D/f3.bin" >&3
if wait_for "$S3-bob.log" "Incoming file transfer #1"; then
    echo "/accept 1" >&4
else
    fail "S3: bob 未收到 incoming 公告"
fi
# 等 alice 进入发送（进度行出现）后立即 cancel——保证 cancel 在发送中途生效
if wait_for "$S3-alice.log" "发送中 #1"; then
    pass "S3: alice 进入发送"
    echo "/cancel 1" >&3
else
    fail "S3: alice 未见进度行"
fi
wait_for "$S3-alice.log" "已取消 #1。" \
    && pass "S3: alice 本地取消提示" || fail "S3: alice 本地取消提示缺失"
# cancel 公告只发给对方（服务器 file_transfer.c：Notify the other party）——查 bob 日志
wait_for "$S3-bob.log" "File transfer #1 cancelled by alice" \
    && pass "S3: bob 见 cancel 公告" || fail "S3: bob 未见 cancel 公告"
wait_gone "$S3/downloads/f3.bin" \
    && pass "S3: bob 半成品已删除" || fail "S3: bob 半成品残留"
echo "/quit" >&3
echo "/quit" >&4
wait $A_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&-

# ===== 场景④ 目标不在线：错误(200) 后槽位释放，第二次发送成功 =====
S4=$D/s4
mkdir -p "$S4"
head -c 65536 /dev/urandom > "$D/f4.bin"
start_srv s4
A_PID=$(client_up s4 alice)
exec 3>$D/s4-alice.in
B_PID=$(client_up s4 bob)
exec 4>$D/s4-bob.in
login 3 alice password123 "$S4-alice.log"
login 4 bob password456 "$S4-bob.log"
echo "/sendfile nobody $D/f4.bin" >&3
wait_for "$S4-alice.log" "错误(200): Target user not found" \
    && pass "S4: 错误 200 显示" || fail "S4: 错误 200 未显示"
# 槽位释放实证：同一连接内第二次发送走通（tid 重新从 1 起——错误路径不分配 tid）
echo "/sendfile bob $D/f4.bin" >&3
if wait_for "$S4-bob.log" "Incoming file transfer #1 from alice"; then
    echo "/accept 1" >&4
else
    fail "S4: 槽位未释放（第二次发送无公告）"
fi
wait_for "$S4-alice.log" "File transfer #1 complete" \
    && pass "S4: 槽位释放，第二次发送成功" || fail "S4: 第二次发送未完成"
diff -q "$D/f4.bin" "$S4/downloads/f4.bin" \
    && pass "S4: diff 字节一致" || fail "S4: 字节不一致"
echo "/quit" >&3
echo "/quit" >&4
wait $A_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&-

# ===== 场景⑤ 非法 tid：bob /accept 999 → 本地提示，不发帧 =====
S5=$D/s5
mkdir -p "$S5"
start_srv s5
B_PID=$(client_up s5 bob)
exec 3>$D/s5-bob.in
login 3 bob password456 "$S5-bob.log"
echo "/accept 999" >&3
wait_for "$S5-bob.log" "没有 #999 的传输记录。" \
    && pass "S5: 非法 tid 本地提示" || fail "S5: 非法 tid 提示缺失"
echo "/quit" >&3
wait $B_PID 2>/dev/null
stop_srv
exec 3>&-

# ===== 场景⑥ 同名加序号：第二次发送同名 → "f6.bin (1)" 且 diff 通过 =====
S6=$D/s6
mkdir -p "$S6"
head -c 65536 /dev/urandom > "$D/f6.bin"
start_srv s6
A_PID=$(client_up s6 alice)
exec 3>$D/s6-alice.in
B_PID=$(client_up s6 bob)
exec 4>$D/s6-bob.in
login 3 alice password123 "$S6-alice.log"
login 4 bob password456 "$S6-bob.log"
echo "/sendfile bob $D/f6.bin" >&3
if wait_for "$S6-bob.log" "Incoming file transfer #1"; then
    echo "/accept 1" >&4
else
    fail "S6: bob 未收到第 1 次公告"
fi
wait_for "$S6-bob.log" "文件 #1 接收完成，已保存: downloads/f6.bin" \
    && pass "S6: 首次落盘" || fail "S6: 首次落盘提示缺失"
# 先等 alice 收到 complete 公告（发送槽释放）再发第二个——
# 否则若 alice 事件循环先处理 stdin 后处理公告帧，会触发槽忙本地拒绝（假 FAIL）
wait_for "$S6-alice.log" "File transfer #1 complete" \
    && pass "S6: alice 槽位释放" || fail "S6: alice 未见 complete 公告"
echo "/sendfile bob $D/f6.bin" >&3
if wait_for "$S6-bob.log" "Incoming file transfer #2"; then
    echo "/accept 2" >&4
else
    fail "S6: bob 未收到第 2 次公告"
fi
wait_for "$S6-bob.log" "文件 #2 接收完成，已保存: downloads/f6.bin (1)" \
    && pass "S6: 同名加序号提示" || fail "S6: 同名序号提示缺失"
diff -q "$D/f6.bin" "$S6/downloads/f6.bin" \
    && pass "S6: 原文件 diff 一致" || fail "S6: 原文件字节不一致"
diff -q "$D/f6.bin" "$S6/downloads/f6.bin (1)" \
    && pass "S6: 序号文件 diff 一致" || fail "S6: 序号文件字节不一致"
echo "/quit" >&3
echo "/quit" >&4
wait $A_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&-

# ===== 场景⑦ 多 pending：alice+charlie 各发一文件，bob 逐个 accept，均 diff 通过 =====
# 注：发送单槽规则下同一发送方无法连发两个（#1 未完成时槽位被占）——
# "多 pending" 的正确形态是多个发送方 → 一个接收方
S7=$D/s7
mkdir -p "$S7"
head -c 65536 /dev/urandom > "$D/f7a.bin"     # 64KB（1 片余量）
head -c 131072 /dev/urandom > "$D/f7b.bin"    # 128KB（3 片，覆盖乱序写盘路径）
start_srv s7
A_PID=$(client_up s7 alice)
exec 3>$D/s7-alice.in
C_PID=$(client_up s7 charlie)
exec 5>$D/s7-charlie.in
B_PID=$(client_up s7 bob)
exec 4>$D/s7-bob.in
login 3 alice password123 "$S7-alice.log"
login 5 charlie password789 "$S7-charlie.log"
login 4 bob password456 "$S7-bob.log"
echo "/sendfile bob $D/f7a.bin" >&3
if wait_for "$S7-alice.log" "已提交文件传输: '$D/f7a.bin'"; then
    echo "/sendfile bob $D/f7b.bin" >&5
else
    fail "S7: alice 提交确认缺失"
fi
# 服务器线程池下 tid 分配顺序不保证——按发送方名字提取 tid，不硬编码编号
if wait_for "$S7-bob.log" "Incoming file transfer #[0-9]* from alice" \
   && wait_for "$S7-bob.log" "Incoming file transfer #[0-9]* from charlie"; then
    pass "S7: 双 pending 公告齐"
else
    fail "S7: bob 未见双公告"
fi
T1=$(grep -o "Incoming file transfer #[0-9]* from alice" "$S7-bob.log" \
     | head -1 | grep -o '[0-9]*')
T2=$(grep -o "Incoming file transfer #[0-9]* from charlie" "$S7-bob.log" \
     | head -1 | grep -o '[0-9]*')
echo "/accept $T1" >&4
wait_for "$S7-bob.log" "文件 #$T1 接收完成，已保存: downloads/f7a.bin" \
    && pass "S7: #$T1 接收完成" || fail "S7: #$T1 未完成"
echo "/accept $T2" >&4
wait_for "$S7-bob.log" "文件 #$T2 接收完成，已保存: downloads/f7b.bin" \
    && pass "S7: #$T2 接收完成" || fail "S7: #$T2 未完成"
diff -q "$D/f7a.bin" "$S7/downloads/f7a.bin" \
    && pass "S7: f7a diff 一致" || fail "S7: f7a 字节不一致"
diff -q "$D/f7b.bin" "$S7/downloads/f7b.bin" \
    && pass "S7: f7b diff 一致" || fail "S7: f7b 字节不一致"
echo "/quit" >&3
echo "/quit" >&5
echo "/quit" >&4
wait $A_PID $C_PID $B_PID 2>/dev/null
stop_srv
exec 3>&- 4>&- 5>&-

# ===== 汇总 =====
DROPS=$(grep -h "exceeded max" "$D"/s*-server.log 2>/dev/null | wc -l)
if [ "$DROPS" -eq 0 ]; then
    pass "服务器零丢包（exceeded max 计数为 0）"
else
    fail "服务器写缓冲丢包日志计数 $DROPS"
fi
rm -rf "$D"
echo "结果: 通过 $PASS / 失败 $FAIL"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
