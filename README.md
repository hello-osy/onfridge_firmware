# 온프리지 펌웨어 개발

## 개발 기간 & 개발 환경

- 2024.12.23~
- Docker를 사용하여 개발 환경을 만들었습니다.

## 브랜치 운영 계획(Gitflow)

- **main**: 안정적인 버전
- **release**: 개발 완성된 버전(버그를 여기서 해결)
- **develop**: feature브랜치 통합
- **feature**: 기능 개발

### 브랜치 운영 절차

1. **feature 브랜치에서 개발**: 각 기능 개발은 `feature/xxx`라는 이름의 브랜치에서 작업합니다.
2. 개발이 완료되면 `develop` 브랜치에 해당 `feature` 브랜치를 병합합니다.
3. `main`과 `release` 브랜치는 오상영이 관리합니다.
4. `release`에서 버그 해결을 완료한 후, `main`, `develop`, `feature` 브랜치에 모두 동기화합니다.

## 개발 환경 세팅

1. `git clone https://github.com/hello-osy/onfridge_firmware.git`
2. uspipd 설치(msi파일): https://github.com/dorssel/usbipd-win/releases/tag/v4.3.0

   Silicon Labs CP210X 드라이버 설치(silabser.inf를 우클릭 후, '설치'): https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads

   설치 완료 후, 컴퓨터 재부팅해주세요.

3. usbipd 명령어를 입력해주세요.

```
usbipd list
```

cp210x관련 번호에 맞게, 아래의 명령어를 수정해서 입력하시면 됩니다.(1-1 대신에 다른 숫자일 수도 있음.)

```
usbipd unbind --busid 1-1
usbipd detach --busid 1-1
usbipd bind --busid 1-1
usbipd attach --busid 1-1 --wsl
```

![alt text](image.png) 이렇게 Attached 상태가 되어야 합니다.

권한이 없어서 Access denied된다면,
`Start-Process PowerShell -Verb runAs -ArgumentList "usbipd attach --busid 1-5 --wsl"` 이런 형식으로 명령어를 입력하시면 됩니다.

4. 호스트(컨테이너 밖) 터미널에서, `wsl`입력해서 wsl접속한 다음, `modprobe cp210x`명령어를 입력하고 나서 `exit`해주세요.

5. docker desktop 프로그램을 실행한다.(미리 다운 받으시면 됩니다.)
6. ESP32 개발 보드와 컴퓨터를 선으로 연결하고 나서, `docker-compose up --build`

   ![alt text](image-1.png) 이렇게 뜨면 잘 된거에요. ctrl+c해서 나가주세요.

7. GND와 0번포트를 수-수 점퍼케이블로 연결하고 나서, esp32개발 보드의 boot와 en버튼을 15초 정도 눌러주세요.(초기화)
8. `docker start onfridge_firmware_container` -> 컨테이너를 실행하고,
   `docker exec -it onfridge_firmware_container bash` -> 컨테이너 안에서 작업해주세요.
9. `pio device list`에 나오는 포트 이름대로 platform.ini 파일의 upload_port를 수정하시면 됩니다.

   ![alt text](image-2.png) 이렇게 뜨면 잘 된거에요.

10. `pio run -t upload`로 코드를 업로드해보세요. `pio run -t uploadfs`를 그 다음에 입력하면 음성 파일도 업로드할 수 있습니다.
11. 회로에 옮겨 꽂았는데 소리 안 나면(speaker.c), en버튼 살짝 누르시면 됩니다.

## Docker 사용 방법

### 1. Docker Desktop 설치 및 실행

- Docker Desktop을 설치하고 실행합니다.

### 2. Docker 이미지 빌드(vscode 터미널에서 하면 됨)

전체 빌드는 처음에 1번만 하시면 됩니다.

- 전체 빌드:

```bash
docker-compose up --build
```

- 전체 빌드 후 도커 컨테이너 백그라운드 실행:

```bash
docker-compose up -d --build
```

- 도커 컨테이너 중지 및 제거

```bash
docker-compose down
```

- docker 빌드 캐시 삭제 명령:

```bash
docker builder prune
docker image prune -a
docker volume prune
docker system prune -a --volumes
```

### 3. 호스트에서 수정한 코드를 도커 컨테이너에 반영

- 도커 컨테이너 내부로 파일 복사하기

```
docker cp sound_receiver.py onfridge_firmware_container:/app/sound_receiver.py
docker cp microphone.c onfridge_firmware_container:/app/microphone.c
```

- 컨테이너 진입

```
docker start onfridge_firmware_container
docker exec -it onfridge_firmware_container bash
```

- 작업 디렉터리 이동(/app)

```
cd /app
```

- 필요한 빌드/적용 작업 수행

```
python sound_receiver.py
```

```
gcc -o microphone microphone.c -lesp32 -lpthread
chmod +x microphone
./microphone
```

### 4. 도커 컨테이너에서 수정한 코드를 호스트에 반영

- 도커 컨테이너에서 호스트로 파일 복사

```
docker cp onfridge_firmware_container:/app/sound_receiver.py ./sound_receiver.py
docker cp onfridge_firmware_container:/app/microphone.c ./microphone.c
```

## 개발할 때

### 1. 로컬 브랜치 생성 및 전환

```

git checkout -b feature/xxx

```

- `feature/xxx`라는 이름의 로컬 브랜치를 생성하고 해당 브랜치로 이동합니다.
- 로컬에 이미 feature/xxx가 있으면 -b는 빼도 됩니다.

### 2. 원격 저장소와 동기화

```

git pull origin feature/xxx

```

- 원격 저장소의 `feature/xxx`와 로컬의 `feature/xxx`를 동기화합니다.

### 3. 개발 후 커밋 및 푸시

- 개발을 완료한 후, 변경 사항을 커밋하고 푸시합니다.

```

git add .
git commit -m "커밋 제목"
git push origin feature/xxx

```

### 4. 브랜치 병합 (feature → develop)

1. `develop` 브랜치로 이동:

```

git checkout develop

```

2. `feature/xxx` 브랜치를 `develop` 브랜치에 병합:

```

git merge feature/xxx

```

## Platformio 명령어

1. ESP32에 코드 업로드

```

pio run -t upload

```

2. ESP32의 SPIFFS에 업로드(음성 파일 같은 거)

```

pio run -t uploadfs

```

3. UART Monitor 확인

```

pio device monitor

```

4. ESP-IDF 프로젝트 설정 변경

```

pio run -t menuconfig

```

5. ESP-IDF 프로젝트 캐시 초기화

```

pio run --target clean

```

6. PlatformIO에서 사용 가능한 직렬 포트를 확인

```

pio device list

```

## 참고 사항

1. 특정 파일만 빌드해서 업로드하고 싶으면 src/CMakeLists.txt파일을 수정하면 됩니다.
2. platformio.ini에 정의된 속도와 .c파일에 정의된 속도가 동일한지 확인하세요.
3. `docker compose up --build`가 지나치게 빠르게 되면서 espidf 관련 오류가 뜨면, docker 캐시를 삭제하고 다시 빌드해주세요.
4.

```

usbipd unbind --busid 1-1
usbipd detach --busid 1-1
usbipd list
usbipd bind --busid 1-1
usbipd attach --busid 1-1 --wsl

dmesg | grep usb
ls -l /dev/ttyUSB0

echo "10c4 ea60" | tee /sys/bus/usb-serial/drivers/cp210x/new_id

```

5. 컴퓨터에 docker-desktop외의 다른 배포판이 wsl기본값으로 설정되어있으면 오류가 생길 수 있습니다.
   `wsl --list --verbose`로 WSL 배포판 이름을 확인하시고, `wsl --unregister <distribution_name>`로 사용하지 않는 WSL 배포판을 삭제해주세요.
6. `aplay received_audio.wav` 형식의 명령어로 wav파일을 재생할 수 있습니다.
7. `hexdump -C received_audio.raw` 형식의 명령어로 raw파일을 볼 수 있습니다.
8. usb를 뺐다가 꽂을 때마다, `usbipd attach --busid 1-1 --wsl`명령어를 입력해줘야 합니다. 그래야 docker container에서 esp32 개발 보드를 인식할 수 있습니다.(usb 선은 웬만하면 뽑지 않는 것이 좋을 것 같습니다.)
9. 도커 컨테이너 내에서 vim 또는 nano로 개발할 수도 있습니다. 개발하신 내용을 호스트에도(컨테이너 밖) 반영해서 git으로 공유해주세요~
