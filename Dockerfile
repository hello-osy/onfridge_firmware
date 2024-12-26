# Python 베이스 이미지 사용
FROM python:3.12-slim as base

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

# Python 의존성 복사 및 설치
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 애플리케이션 소스 파일 복사
COPY . .
COPY ./src /app/src

# VSCode Server 비밀번호 없애기
COPY config.yaml /root/.config/code-server/config.yaml

# VSCode Server 설치
RUN curl -fsSL https://code-server.dev/install.sh | sh

# VSCode Server의 기본 포트 설정
EXPOSE 8080

# VSCode Server 실행 명령
CMD ["code-server", "--bind-addr", "0.0.0.0:8080", "/app"]
