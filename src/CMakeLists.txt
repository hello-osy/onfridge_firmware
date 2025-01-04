idf_component_register(
    SRCS 
        #"microphone.c"
        #"speaker.c"
        "wake_word.cpp"
        "tflite_model.h"
    INCLUDE_DIRS "."
    REQUIRES tflite_micro
)
# 안 쓰는 파일은 왼쪽에 #을 붙여 주석 처리하세요.