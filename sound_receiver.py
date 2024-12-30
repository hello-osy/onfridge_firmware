import serial
import wave
import time

# UART 및 파일 설정
port = '/dev/ttyUSB0'
baud_rate = 115200  # ESP32와 동일한 설정
raw_file = "received_audio.raw"
wav_file = "received_audio.wav"

# WAV 설정
sample_rate = 8000
channels = 1
sample_width = 2
buffer_size = sample_rate * sample_width  # 1초 데이터 (16KB)

try:
    ser = serial.Serial(port=port, baudrate=baud_rate, timeout=2)  # timeout 2초로 설정
    print(f"Connected to {port} at {baud_rate} baud.")

    # ESP32에 명령 전송
    command = "START_RECORDING\n"
    ser.write(command.encode())
    print(f"Command sent to ESP32: {command.strip()}")

    print("Waiting for data...")
    received_data = ser.read_until(b"<DATA_END>")  # 데이터 종료 태그 기준으로 읽기
    if b"<DATA_START>" in received_data:
        start_index = received_data.find(b"<DATA_START>") + len(b"<DATA_START>")
        end_index = received_data.find(b"<DATA_END>")
        received_data = received_data[start_index:end_index]

    if len(received_data) < buffer_size:
        print(f"Warning: Incomplete data received. Padding {buffer_size - len(received_data)} bytes with zeros.")
        received_data += b'\x00' * (buffer_size - len(received_data))

    # RAW 파일로 저장
    with open(raw_file, "wb") as raw:
        raw.write(received_data)

    # WAV 파일로 변환
    print(f"Converting {raw_file} to {wav_file}...")
    with wave.open(wav_file, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(sample_width)
        wav.setframerate(sample_rate)
        wav.writeframes(received_data)
    print(f"Conversion complete. Saved as {wav_file}.")
    
except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print(f"Disconnected from {port}.")
