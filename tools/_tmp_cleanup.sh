#!/bin/bash
pkill -f 'chat_server 18084' 2>/dev/null
pkill -f '/mnt/d/AAA_Game_XueXiBan/ShareUbuntu/CC/TCPClient/bin/tcp_client' 2>/dev/null
sleep 0.5
pgrep -af 'chat_server 18084|tcp_client' || echo CLEAN
