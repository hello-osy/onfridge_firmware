#CMake 최소 요구 버전 설정
cmake_minimum_required(VERSION 3.16.0)

# 프로젝트 이름을 "onfridge_firmware"로 설정
project(onfridge_firmware)

# Include src directory
add_subdirectory(src)

# Register the component
idf_component_register(
    SRCS ${SRC_FILES}  # Use variable defined in src/CMakeLists.txt
    INCLUDE_DIRS "components/esp-tflite-micro/include"
    REQUIRES esp_timer nvs_flash
)