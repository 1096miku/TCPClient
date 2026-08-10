#!/bin/bash
# M3 冒烟 e2e：群组全场景（真实服务器 + 三客户端）
# 场景: ①创建(含重名203) ②加入(含重复202) ③群聊(含发送者回显/非成员201/群不存在201)
#       ④离开(含公告/最后一人删群) ⑤缺参用法提示 + /help
# 用法: WSL 内 bash tools/e2e_m3.sh；日志写 /tmp/（重启自动清理，不落仓库）
set -u

SRV=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPServer
CLI=/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient
PORT=18083   # 避开残留进程
LOG=/tmp/e2e_m3

cd "$SRV"
[ -x ./bin/chat_server ] || (cmake -B build-wsl -S . >/dev/null && cmake --build build-wsl >/dev/null)
./bin/chat_server $PORT data/passwd.txt >$LOG-server.log 2>&1 &
SRV_PID=$!
sleep 1

cd "$CLI"
# alice: 登录 → 创建群 → 重名创建(203) → 群聊 → 向不存在群发消息(201) → 最后一人离开(删群) → 退出
(
    sleep 0.2
    printf 'alice\npassword123\n'
    sleep 2.5
    echo '/gcreate 学习小组'
    sleep 0.5
    echo '/gcreate 学习小组'
    sleep 0.5
    echo '/gmsg 学习小组 今天学什么'
    sleep 2.5
    echo '/gmsg 幽灵群 hi'
    sleep 2
    echo '/gleave 学习小组'
    sleep 1.5
    echo '/quit'
    sleep 1
) | ./bin/tcp_client 127.0.0.1 $PORT >$LOG-alice.log 2>&1 &
A_PID=$!

# bob: 登录 → 加入群 → 重复加入(202) → 群聊 → 离开 → 退出
(
    sleep 1.5
    printf 'bob\npassword456\n'
    sleep 1.5
    echo '/gjoin 学习小组'
    sleep 1
    echo '/gjoin 学习小组'
    sleep 2
    echo '/gmsg 学习小组 收到'
    sleep 1
    echo '/gleave 学习小组'
    sleep 1
    echo '/quit'
    sleep 1
) | ./bin/tcp_client 127.0.0.1 $PORT >$LOG-bob.log 2>&1 &
B_PID=$!

# charlie: 登录 → 非成员向群发消息(201) → 缺参用法提示 → /help → 退出
(
    sleep 3
    printf 'charlie\npassword789\n'
    sleep 2
    echo '/gmsg 学习小组 hi'
    sleep 1
    echo '/gmsg 学习小组'
    sleep 0.5
    echo '/gjoin'
    sleep 0.5
    echo '/help'
    sleep 1
    echo '/quit'
    sleep 1
) | ./bin/tcp_client 127.0.0.1 $PORT >$LOG-charlie.log 2>&1 &
C_PID=$!

wait $A_PID $B_PID $C_PID
kill $SRV_PID 2>/dev/null
wait $SRV_PID 2>/dev/null

echo "================ alice 输出 ================"
cat $LOG-alice.log
echo "================ bob 输出 ================"
cat $LOG-bob.log
echo "================ charlie 输出 ================"
cat $LOG-charlie.log
