cmake_minimum_required(VERSION 3.16.0)

# ESP-IDF 프로젝트 초기화
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(onfridge_firmware)

# TensorFlow Lite Micro 경로 설정
set(TFLITE_MICRO_PATH $ENV{TFLITE_MICRO_PATH})

# TensorFlow Lite Micro 소스 경로 및 헤더 경로 추가
include_directories(
    ${TFLITE_MICRO_PATH}
    ${TFLITE_MICRO_PATH}/tensorflow
    ${TFLITE_MICRO_PATH}/tensorflow/lite
    ${TFLITE_MICRO_PATH}/tensorflow/lite/micro
    ${TFLITE_MICRO_PATH}/third_party/flatbuffers/include
)

# TensorFlow Lite Micro 소스 코드 추가
set(TFLITE_MICRO_SRCS
    ${TFLITE_MICRO_PATH}/tensorflow/lite/micro/*.cc
    ${TFLITE_MICRO_PATH}/tensorflow/lite/core/api/*.cc
)

# 프로젝트 소스 코드 추가
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${TFLITE_MICRO_SRCS})