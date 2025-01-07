import tensorflow as tf

interpreter = tf.lite.Interpreter(model_path="./data/wake_word_model.tflite")
interpreter.allocate_tensors()

# 모델에 사용된 연산자 확인
try:
    details = interpreter._get_ops_details()
    for op in details:
        print(op)
except AttributeError:
    print("TensorFlow 버전에서 '_get_ops_details()'를 사용할 수 없습니다.")
    print("TensorFlow 버전을 확인하거나 다른 방법을 시도하세요.")

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
