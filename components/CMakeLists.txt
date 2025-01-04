idf_component_register(
    SRCS
        "tflite_micro/tensorflow/lite/micro/micro_interpreter.cc"
        "tflite_micro/tensorflow/lite/micro/micro_allocator.cc"
        "tflite_micro/tensorflow/lite/micro/micro_op_resolver.cc"
        "tflite_micro/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc"
        "tflite_micro/tensorflow/lite/micro/micro_utils.cc"
    INCLUDE_DIRS
        "tflite_micro/tensorflow"                    # 명시적으로 필요한 경로
        "tflite_micro/tensorflow/lite"               # 서브 디렉토리 명시
        "tflite_micro/tensorflow/lite/micro"         # 마이크로 디렉토리
        "tflite_micro/tensorflow/lite/schema"        # 스키마 파일이 포함된 디렉토리
        "flatbuffers/include"                        # FlatBuffers 헤더 경로
)
