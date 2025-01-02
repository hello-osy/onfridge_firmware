import serial
import wave
import time

# UART 및 파일 설정
port = '/dev/ttyUSB0'  # ESP32와 연결된 포트
baud_rate = 115200  # ESP32와 동일한 UART 설정
raw_file = "received_audio.raw"  # 수신된 데이터를 저장할 RAW 파일
wav_file = "received_audio.wav"  # 변환된 WAV 파일

# WAV 설정
sample_rate = 16000  # 샘플링 속도 (ESP32 설정과 동일)
channels = 1  # 오디오 채널 수 (모노)
sample_width = 2  # 샘플 크기 (16비트)
recording_size = 5 # 녹음 시간
buffer_size = sample_rate * sample_width * recording_size # recording_size초 데이터 (16*recording_size)KB

try:
    # UART 연결 설정
    ser = serial.Serial(port=port, baudrate=baud_rate, timeout=5)
    
    # USB 포트가 닫힐 때 리셋되지 않도록
    ser.dtr = False  # DTR 플래그 비활성화
    ser.rts = False  # RTS 플래그 비활성화
    
    print(f"Connected to {port} at {baud_rate} baud.")

    # ESP32에 START_RECORDING 명령 전송
    command = "START_RECORDING\n"
    
    # ESP32로부터 데이터 수신
    received_data = b""

    # 첫 번째 유효 데이터 수신 대기
    while True:  # 정상적인 데이터 수신 전까지 계속 명령 전송
        ser.write(command.encode())
        print(f"Command sent to ESP32: {command.strip()}")
        
        # <DATA_START> 태그가 포함된 데이터 수신 대기
        chunk = ser.read_until(b"<DATA_END>")
        if b"<DATA_START>" in chunk and b"<DATA_END>" in chunk:
            print("Valid data start detected, recording initial data...")
            
            # 첫 태그 사이 데이터 추출 및 기록
            start_index = chunk.find(b"<DATA_START>") + len(b"<DATA_START>")
            end_index = chunk.find(b"<DATA_END>")
            initial_data = chunk[start_index:end_index]
            received_data += initial_data
            print(f"Initial data recorded: {len(initial_data)} bytes")
            break
        else:
            print(f"No valid data start received, retrying...\nRaw chunk: {chunk}")
            time.sleep(1)  # 1초 대기 후 명령 재전송

    # 5초 동안 추가 데이터 수신
    for second in range(recording_size - 1):  # 이미 1초 데이터 기록됨, 나머지 4초 처리
        print(f"Waiting for data for second {second + 2}...")
        while True:
            chunk = ser.read_until(b"<DATA_END>")  # <DATA_END>까지 읽기
            if b"<DATA_START>" in chunk and b"<DATA_END>" in chunk:
                # 유효한 데이터 추출
                start_index = chunk.find(b"<DATA_START>") + len(b"<DATA_START>")
                end_index = chunk.find(b"<DATA_END>")
                data_chunk = chunk[start_index:end_index]

                # 데이터 크기 검증
                if len(data_chunk) > 0:
                    received_data += data_chunk
                    print(f"Received data for second {second + 2}: {len(data_chunk)} bytes")
                    break
                else:
                    print(f"Empty data received for second {second + 2}, retrying...")
            else:
                # 무효한 데이터 무시
                if b"<DATA_END>" not in chunk:
                    print(f"Invalid data received (missing <DATA_END>): {chunk}")
                elif b"<DATA_START>" not in chunk:
                    print(f"Invalid data received (missing <DATA_START>): {chunk}")
                else:
                    print(f"Unknown invalid data: {chunk}")
            # 재시도 간 짧은 대기
            time.sleep(0.1)


    # 데이터 크기 확인 및 보정
    if len(received_data) < buffer_size:
        padding_size = buffer_size - len(received_data)
        received_data += b'\x00' * padding_size
        print(f"Padding with zeros: {padding_size} bytes")
    elif len(received_data) > buffer_size:
        received_data = received_data[:buffer_size]
        print(f"Truncating excess data")

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
    ser.close()
    print(f"Disconnected from {port}.")
