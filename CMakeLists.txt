cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project name
project(onfridge_firmware)

# 추가 컴포넌트 디렉토리 설정
set(EXTRA_COMPONENT_DIRS "/app/components/esp-tflite-micro")