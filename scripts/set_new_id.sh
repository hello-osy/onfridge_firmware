#!/bin/bash

# `new_id` 설정
echo "10c4 ea60" | tee /sys/bus/usb-serial/drivers/cp210x/new_id

# 권한 부여
chmod 666 /dev/ttyUSB0

# 실행 중인 컨테이너를 계속 실행하도록 하기 위해 대기
while true; do
    sleep 3600
done
