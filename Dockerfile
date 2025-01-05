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
    udev \
    dos2unix \
    alsa-utils \
    bsdmainutils \
    nano \
    vim \
    gcc \
    g++ \
    tmux \
    minicom \
    socat \
    lsof \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# ESP-IDF 클론 및 설치
RUN git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git /esp-idf
RUN /esp-idf/install.sh

# ESP-IDF 환경 변수 추가
ENV IDF_PATH="/esp-idf"
ENV PATH="${IDF_PATH}/tools:$PATH"
RUN echo "source ${IDF_PATH}/export.sh" >> /root/.bashrc

# TensorFlow Lite Micro 소스코드 클론 및 설치
RUN git clone --recurse-submodules https://github.com/espressif/esp-tflite-micro.git /app/components/esp-tflite-micro
RUN cd /app/components/esp-tflite-micro && git rev-parse --is-inside-work-tree || (echo "Invalid git repository" && exit 1)

# PlatformIO 설치
RUN pip install --no-cache-dir platformio

# Python 의존성 복사 및 설치
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 프로젝트 전체 복사
COPY . /app

# 스크립트를 복사하고 실행 권한 부여
RUN dos2unix /app/scripts/set_new_id.sh && chmod +x /app/scripts/set_new_id.sh

# 컨테이너가 시작될 때 ESP-IDF 환경 설정 활성화
SHELL ["/bin/bash", "-c"]

# 컨테이너가 시작될 때마다 스크립트를 실행하도록 설정
CMD ["bash", "-c", "source ${IDF_PATH}/export.sh && /app/scripts/set_new_id.sh && exec bash"]
#CMD ["/app/scripts/set_new_id.sh"]
#CMD ["bash"]