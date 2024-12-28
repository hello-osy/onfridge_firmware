# Python 베이스 이미지 사용
FROM python:3.12-slim AS base

# 작업 디렉토리 설정
WORKDIR /app

# ESP32와 Python 환경을 위한 필수 패키지 설치
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    wget \
    cmake \
    ninja-build \
    gperf \
    python3-pip \
    python3-setuptools \
    python3-venv \
    libffi-dev \
    libssl-dev \
    libusb-1.0-0-dev \
    pkg-config \
    curl \
    unzip \
    build-essential \
    kmod \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# ESP-IDF 클론 및 설치
RUN git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git /esp-idf
RUN /esp-idf/install.sh

# PlatformIO 설치
RUN pip install --no-cache-dir platformio


# ESP-IDF 환경 변수 추가
ENV PATH="/esp-idf/tools:$PATH"
ENV IDF_PATH="/esp-idf"

# 컨테이너 내 udev 설치
RUN apt-get update && apt-get install -y udev

# Python 의존성 복사 및 설치
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 호스트의 모든 파일을 컨테이너로 복사
COPY . .

# 기본 실행 명령 설정
CMD ["bash"]