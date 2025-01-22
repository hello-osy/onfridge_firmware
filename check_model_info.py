import tensorflow as tf

# 모델의 입력 및 출력 타입 확인
interpreter = tf.lite.Interpreter(model_path="./data/wake_word_model.tflite")
interpreter.allocate_tensors()

# 입력 및 출력 텐서 정보 가져오기
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# TensorFlow Lite 데이터 타입 매핑
dtype_mapping = {
    tf.float32: "FLOAT32",
    tf.int8: "INT8",
    tf.uint8: "UINT8",
    tf.int32: "INT32",
    tf.uint32: "UINT32"
}

print("Input tensor information:")
print(f"  Name: {input_details[0]['name']}")
print(f"  Type: {dtype_mapping.get(input_details[0]['dtype'], str(input_details[0]['dtype']))}")
print(f"  Scale: {input_details[0]['quantization'][0]}")
print(f"  Zero Point: {input_details[0]['quantization'][1]}")

print("\nOutput tensor information:")
print(f"  Name: {output_details[0]['name']}")
print(f"  Type: {dtype_mapping.get(output_details[0]['dtype'], str(output_details[0]['dtype']))}")
print(f"  Scale: {output_details[0]['quantization'][0]}")
print(f"  Zero Point: {output_details[0]['quantization'][1]}")

# 텐서 타입 확인
if input_details[0]['dtype'] == tf.int8:
    print("\nInput tensor type matches kTfLiteInt8.")
else:
    print("\nInput tensor type does NOT match kTfLiteInt8.")

if output_details[0]['dtype'] == tf.int8:
    print("Output tensor type matches kTfLiteInt8.")
else:
    print("Output tensor type does NOT match kTfLiteInt8.")



# import tensorflow as tf

# # TensorFlow Lite 모델 파일 경로
# tflite_model_path = "./data/wake_word_model.tflite"

# # TensorFlow Lite 인터프리터 초기화
# interpreter = tf.lite.Interpreter(model_path=tflite_model_path)

# # Tensor 할당
# interpreter.allocate_tensors()

# # 입력 및 출력 텐서 정보 확인
# input_details = interpreter.get_input_details()
# output_details = interpreter.get_output_details()

# # 입출력 정보 출력
# def print_tensor_details(details, tensor_type):
#     print(f"\n{tensor_type} Tensor Details:")
#     for idx, tensor in enumerate(details):
#         print(f" - Tensor {idx + 1}:")
#         print(f"   Name: {tensor['name']}")
#         print(f"   Index: {tensor['index']}")
#         print(f"   Shape: {tensor['shape']}")
#         print(f"   Shape Signature: {tensor['shape_signature']}")
#         print(f"   Data Type: {tensor['dtype']}")

#         if 'quantization' in tensor:
#             print(f"   Quantization: Scale = {tensor['quantization'][0]}, Zero Point = {tensor['quantization'][1]}")
#         else:
#             print("   Quantization: None")

#         # dims 정보 출력
#         if 'shape' in tensor:
#             dims = tensor['shape']
#             print(f"   Dims (Rank: {len(dims)}):")
#             for i, dim in enumerate(dims):
#                 print(f"     Dim {i}: {dim}")
#         else:
#             print("   Dims: None")
#         print("   -----------------------------")

# # 상세 정보 출력
# print_tensor_details(input_details, "Input")
# print_tensor_details(output_details, "Output")



# import tensorflow as tf

# interpreter = tf.lite.Interpreter(model_path="./data/wake_word_model.tflite")
# interpreter.allocate_tensors()

# # 모델에 사용된 연산자 확인
# try:
#     details = interpreter._get_ops_details()
#     for op in details:
#         print(op)
# except AttributeError:
#     print("TensorFlow 버전에서 '_get_ops_details()'를 사용할 수 없습니다.")
#     print("TensorFlow 버전을 확인하거나 다른 방법을 시도하세요.")




# import tensorflow as tf
# import numpy as np
# import os
# os.environ['TF_ENABLE_ONEDNN_OPTS'] = '0'

# # Load model and allocate tensors
# interpreter = tf.lite.Interpreter(model_path="./data/wake_word_model.tflite", experimental_delegates=[])
# interpreter.allocate_tensors()

# # Get input/output details
# input_details = interpreter.get_input_details()
# output_details = interpreter.get_output_details()

# # Prepare random input data
# input_data = np.random.randint(-128, 127, input_details[0]['shape'], dtype=np.int8)
# interpreter.set_tensor(input_details[0]['index'], input_data)

# # Debug: Check input data
# print("Input data shape:", input_data.shape)
# print("Input data:", input_data)

# # Run inference
# try:
#     interpreter.invoke()
#     output_data = interpreter.get_tensor(output_details[0]['index'])
#     print("Output:", output_data)
# except RuntimeError as e:
#     print("Error during inference:", e)
