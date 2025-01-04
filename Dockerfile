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
    tmux \
    minicom \
    socat \
    lsof \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# ESP-IDF 클론 및 설치
RUN git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git /esp-idf
RUN /esp-idf/install.sh

# TensorFlow Lite Micro 소스코드 클론 및 설치
RUN git clone --recurse-submodules https://github.com/tensorflow/tflite-micro.git /app/components/tflite_micro

# FlatBuffers 다운로드 및 빌드
RUN git clone https://github.com/google/flatbuffers.git /app/components/flatbuffers && \
    cd /app/components/flatbuffers && \
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DFLATBUFFERS_BUILD_TESTS=OFF . && \
    make && \
    make install

# FlatBuffers 헤더 파일 경로 설정
ENV FLATBUFFERS_INCLUDE_PATH="/app/components/flatbuffers/include"

# PlatformIO 설치
RUN pip install --no-cache-dir platformio

# ESP-IDF 환경 변수 추가
ENV PATH="/esp-idf/tools:$PATH"
ENV IDF_PATH="/esp-idf"

# Python 의존성 복사 및 설치
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 호스트의 모든 파일을 컨테이너로 복사
COPY . .

# Windows 파일 줄 바꿈 형식 변환 및 실행 권한 추가
RUN dos2unix /app/scripts/set_new_id.sh && chmod +x /app/scripts/set_new_id.sh

# 컨테이너가 시작될 때마다 스크립트를 실행하도록 설정
CMD ["/app/scripts/set_new_id.sh"]
#CMD ["bash"]