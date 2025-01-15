# ESP-IDF 기반 이미지 사용
FROM esp-idf-base AS base

WORKDIR /app

# Python 의존성 복사 및 설치
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 프로젝트 전체 복사
COPY . /app

# 스크립트를 복사하고 실행 권한 부여
RUN dos2unix /app/scripts/set_new_id.sh && chmod +x /app/scripts/set_new_id.sh
RUN dos2unix /esp-idf/export.sh && chmod +x /esp-idf/export.sh

# ENTRYPOINT에서 export.sh와 set_new_id.sh를 실행한 후 idf.py 실행
ENTRYPOINT ["/bin/bash", "-c", "source /esp-idf/export.sh && /app/scripts/set_new_id.sh && exec \"$@\"", "--"]

# 기본 명령어 설정
CMD ["idf.py", "build", "--no-hints"]
#CMD ["bash"]

# RUN=> 이미지 빌드
# CMD=> 컨테이너가 시작될 때 실행될 기본 명령어, 컨테이너의 기본 명령을 설정하지만 사용자가 다른 명령어를 제공하면 무시됩니다.
# ENTRYPOINT=> 컨테이너의 고정 실행 프로세스를 설정하며, 사용자 입력이 결합됩니다.