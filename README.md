# 온프리지 펌웨어 개발

## 개발 기간

- 2024.12.23~

## 개발 환경 세팅

Docker를 사용하여 개발 환경을 만들었습니다.

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

1. ```git clone https://github.com/hello-osy/onfridge_firmware.git```
2. docker desktop 프로그램을 실행한다.(미리 다운 받으시면 됩니다.)
3. ```docker-compose up --build```
4. uspipd를 (https://github.com/dorssel/usbipd-win/releases/tag/v4.3.0 여기서) msi파일 받아서 설치해주세요.
Silicon Labs CP210X 드라이버를 설치해주세요. (설치 방법: https://manuals.plus/ko/silicon-labs/cp210x-universal-windows-driver-software-manual)

5. usbipd 명령어를 입력해주세요.
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

6. GND와 0번포트를 수-수 점퍼케이블로 연결하고 나서, esp32개발 보드의 boot와 en버튼을 15초 정도 눌러주세요.(초기화)
7. ```docker start onfridge_firmware_container``` -> 컨테이너를 실행하고, 
```docker exec -it onfridge_firmware_container bash``` -> 컨테이너 안에서 작업해주세요.
8. ```pio device list```에 나오는 포트 이름대로 platform.ini 파일의 upload_port를 수정하시면 됩니다.
9. ```pio run -t upload```로 코드를 업로드해보세요. ```pio run -t uploadfs```를 그 다음에 입력하면 음성 파일도 업로드할 수 있습니다. 
10. 회로에 옮겨 꽂았는데 소리 안 나면, en버튼 살짝 누르시면 됩니다.

## Docker 사용 방법

### 1. Docker Desktop 설치 및 실행

- Docker Desktop을 설치하고 실행합니다.

### 2. Docker 이미지 빌드(vscode 터미널에서 하면 됨)

- 전체 빌드(처음에만):

```bash
docker-compose up --build
```

- 개발할 때:

```bash
docker-compose down
docker-compose up -d --build
```
이렇게 해야, 수정된 코드가 컨테이너에 반영됩니다.

- docker 빌드 캐시 삭제 명령:
```bash
docker builder prune
docker image prune -a
docker volume prune
docker system prune -a --volumes
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
3. ESP32의 로그는 platformio ide에서 제공하는 Serial Monitor로 확인할 수 있습니다.
4. ```docker compose up --build```가 지나치게 빠르게 되면서 espidf 관련 오류가 뜨면, docker 캐시를 삭제하고 다시 빌드해주세요.
5. 
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