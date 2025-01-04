#CMake 최소 요구 버전 설정
cmake_minimum_required(VERSION 3.16.0)

# ESP-IDF 프로젝트 초기화
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 프로젝트 이름을 "onfridge_firmware"로 설정
project(onfridge_firmware)

# 추가 컴포넌트 디렉토리 설정
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components
)