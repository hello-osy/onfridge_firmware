# esp32/main 디렉터리의 모든 파일을 빌드에 포함
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/esp32/main/*.*)

# esp32/main 디렉터리를 빌드에 등록
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")