import serial
import wave
import time

# UART 및 파일 설정
port = '/dev/ttyUSB0'  # ESP32와 연결된 포트
baud_rate = 115200  # ESP32와 동일한 UART 설정
raw_file = "received_audio.raw"  # 수신된 데이터를 저장할 RAW 파일
wav_file = "received_audio.wav"  # 변환된 WAV 파일

# WAV 설정
sample_rate = 8000  # 샘플링 속도 (ESP32 설정과 동일)
channels = 1  # 오디오 채널 수 (모노)
sample_width = 2  # 샘플 크기 (16비트)
buffer_size = sample_rate * sample_width  # 1초 데이터 (16KB)

MAX_RETRIES = 3  # 데이터 수신 최대 재시도 횟수
WAIT_TIME = 2  # ESP32 데이터 준비 대기 시간 (초)

try:
    # UART 연결 설정
    ser = serial.Serial(port=port, baudrate=baud_rate, timeout=10)  # timeout 10초로 설정
    
    # USB 포트가 닫힐 때 리셋되지 않도록
    ser.dtr = False  # DTR 플래그 비활성화
    ser.rts = False  # RTS 플래그 비활성화
    
    print(f"Connected to {port} at {baud_rate} baud.")

    # ESP32에 START_RECORDING 명령 전송
    command = "START_RECORDING\n"
    for attempt in range(MAX_RETRIES):
        ser.write(command.encode())
        print(f"Command sent to ESP32: {command.strip()} (Attempt {attempt + 1})")
        
        # ESP32 데이터 준비 대기
        time.sleep(WAIT_TIME)
        print("Waiting for data...")
        
        # ESP32로부터 데이터 수신
        received_data = ser.read_until(b"<DATA_END>")
        if b"<DATA_START>" in received_data:
            # 시작 및 종료 태그 제거
            start_index = received_data.find(b"<DATA_START>") + len(b"<DATA_START>")
            end_index = received_data.find(b"<DATA_END>")
            received_data = received_data[start_index:end_index]
            break  # 성공적으로 데이터 수신 시 루프 종료
        else:
            print(f"Attempt {attempt + 1} failed. Retrying...")
    else:
        raise RuntimeError("Failed to receive valid data after maximum retries.")
    
    print(f"Received {len(received_data)} bytes from ESP32.")
    if len(received_data) == 0:
        print("Warning: No data received. Check UART connection.")

    # 데이터가 충분하지 않으면 0으로 패딩
    if len(received_data) < buffer_size:
        padding_size = buffer_size - len(received_data)
        print(f"Warning: Incomplete data received. Padding {padding_size} bytes with zeros.")
        received_data += b'\x00' * padding_size

    # 수신된 데이터를 RAW 파일로 저장
    with open(raw_file, "wb") as raw:
        raw.write(received_data)
    print(f"Raw data saved as {raw_file}.")

    # RAW 데이터를 WAV 파일로 변환
    print(f"Converting {raw_file} to {wav_file}...")
    with wave.open(wav_file, "wb") as wav:
        wav.setnchannels(channels)  # 채널 수 설정
        wav.setsampwidth(sample_width)  # 샘플 크기 설정
        wav.setframerate(sample_rate)  # 샘플링 속도 설정
        wav.writeframes(received_data)  # 데이터 쓰기
    print(f"Conversion complete. Saved as {wav_file}.")
    
except Exception as e:
    # 예외 처리
    print(f"Error: {e}")
finally:
    # UART 연결 종료 전 지연
    print("Waiting before closing port...")
    time.sleep(2)  # 2초 대기
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print(f"Disconnected from {port}.")
