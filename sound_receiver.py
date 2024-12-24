import serial
import wave
import time

# UART 및 파일 설정
port = 'COM3'
baud_rate = 921600  # ESP32와 동일한 설정
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

    # ESP32 명령 응답 확인
    time.sleep(1)  # ESP32가 응답하도록 대기
    if ser.in_waiting > 0:
        response = ser.readline().decode('utf-8', errors='ignore').strip()
        print(f"ESP32 Response: {response}")
    else:
        print("No response from ESP32 after command. Continuing...")

    # 로그 수신 및 데이터 수신
    print("Receiving 1 second of audio data...")
    received_data = bytearray()

    while len(received_data) < buffer_size:
        # 512 바이트씩 데이터를 읽음
        data = ser.read(512)
        if not data:
            print(f"No data received. Total bytes received so far: {len(received_data)}")
            break  # 데이터가 없으면 반복 종료

        received_data.extend(data)
        print(f"Received {len(received_data)} bytes out of {buffer_size}")

    # 받은 데이터가 부족할 경우 패딩
    if len(received_data) < buffer_size:
        print(f"Warning: Received incomplete data. Padding {buffer_size - len(received_data)} bytes with zeros.")
        received_data.extend([0] * (buffer_size - len(received_data)))

    print(f"Data reception complete. Total bytes: {len(received_data)}")

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
    
    # 데이터 수신 시작 및 종료 구분자 정의
    DATA_START = "<DATA_START>"
    DATA_END = "<DATA_END>"

    received_data = bytearray()
    is_receiving = False
    log_timeout = 5  # 5초 동안 로그를 수신

    start_time = time.time()
    while time.time() - start_time < log_timeout:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line == DATA_START:
                is_receiving = True
                print("Data reception started.")
                continue
            elif line == DATA_END:
                print("Data reception ended.")
                break

            if is_receiving:
                received_data.extend(line.encode())  # 데이터 추가
                print(f"Received {len(received_data)} bytes so far.")

except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print(f"Disconnected from {port}.")
