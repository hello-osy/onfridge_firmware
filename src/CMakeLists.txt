idf_component_register(
    SRCS
        #"microphone.c"
        #"speaker.c"
        #"wake_word.cpp"
        "check_memory_status.cpp"
    INCLUDE_DIRS "."
)
# 빌드 안 할 파일 왼쪽에 #붙이면 주석으로 처리됩니다.

# C++ 컴파일러를 사용할 파일 설정
set_source_files_properties(
    #"wake_word.cpp"
    "check_memory_status.cpp"
    PROPERTIES LANGUAGE CXX
)