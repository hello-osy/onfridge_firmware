#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"  // 필요한 연산자만 등록할 수 있음.
#include "tensorflow/lite/micro/micro_interpreter.h" // TensorFlow Lite Micro 인터프리터를 정의하는 헤더 파일. 모델 데이터를 실행하고, 입력/출력 텐서를 관리함.
#include "tensorflow/lite/schema/schema_generated.h" // TensorFlow Lite 모델의 스키마 정의를 포함하는 헤더 파일. 모델의 버전 및 구조를 확인함.
#include "tensorflow/lite/micro/micro_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h" // I2S (마이크 입력 처리)를 위한 ESP32 드라이버.
#include "esp_log.h"  // ESP32 로깅 유틸리티.
#include "esp_system.h" // ESP32 시스템 관련 유틸리티.
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include <iostream>
#include <vector>
#include <inttypes.h> // PRIu32 매크로를 사용하기 위해 필요합니다.
#include <cmath>
#include "mbedtls/md5.h" // 혹은 sha256

constexpr i2s_port_t I2S_NUM = I2S_NUM_0; // I2S 채널 번호
constexpr int DMA_BUFFER_COUNT = 3; // DMA 버퍼 개수
constexpr int DMA_BUFFER_SIZE = 4000; // 각 DMA 버퍼 크기 (바이트 단위, 1개당 최대 4092)
constexpr int DATA_BIT_WIDTH = 16; // 데이터 비트 폭 (16비트 PCM)
constexpr int SAMPLE_RATE = 4000; // 입력 데이터 샘플링 속도 (Hz)
constexpr int DMA_FRAME_NUM = DMA_BUFFER_SIZE / (DATA_BIT_WIDTH / 8); // 각 DMA 버퍼에 저장할 샘플 프레임 개수 (프레임당 16비트 데이터를 사용)

constexpr int INPUT_SAMPLES = SAMPLE_RATE; // 초당 샘플 개수 (16비트 PCM)

constexpr int TENSOR_ARENA_SIZE = 60 * 1024; // 텐서 연산 메모리 크기
constexpr int MAX_MODEL_SIZE = 4 * 1024; // 모델 데이터 최대 크기

static const char *TAG = "WAKE_WORD";

// I2S 하드웨어의 데이터 흐름 설명
// 1. 데이터가 수신되면 I2S 하드웨어는 FIFO(512바이트 크기)에 데이터를 임시 저장합니다.
// 2. DMA는 FIFO에서 데이터를 읽어와 메모리에 저장합니다. (이 과정은 ESP32의 I2S 드라이버가 자동으로 처리합니다.)
// 3. i2s_channel_read 함수는 DMA 버퍼에 저장된 데이터를 사용자가 제공한 버퍼(예: block.data)로 복사합니다.
//
// 데이터 흐름:
// FIFO (512바이트) --[자동 관리]--> DMA --[i2s_channel_read 함수]--> block.data

// 타입 관련 주의사항
// - uint8_t, uint16_t, int8_t, int16_t 등의 타입은 데이터 해석에 차이를 만듭니다.
// - 메모리 블록은 부호가 없으므로, 보통 **uint8_t (부호 없는 8비트 정수)**로 데이터를 정의하는 것이 일반적입니다.
// - 오디오 데이터처럼 부호가 중요한 경우 **int8_t (부호 있는 8비트 정수)**로 정의합니다.
//
// - uint8_t를 사용하면 해당 메모리 블록을 다양한 방식으로 해석할 수 있습니다 (int8_t, float, int32_t 등).
// - 반면, int8_t를 사용하면 해당 메모리 블록은 부호 있는 8비트 정수로만 해석됩니다.
//
// 요약: 부호가 필요 없는 데이터는 uint8_t를, 부호가 필요한 데이터는 int8_t를 사용하세요.

// 모델은 (1,4000) numpy.int8을 입력으로 받습니다.
// TFLM에서 텐서의 실제 메모리 레이아웃은 1차원으로 연속 저장되기 때문에, int8_t 4000개를 연속해서 복사하면 그것이 곧 (1,4000) 텐서의 메모리와 동일합니다.
// memcpy(input_tensor->data.int8, s_input_buffer, 4000) 해주면, TFLM 내부에서는 input_tensor가 (1, 4000) 형태라고 인식하여 올바른 차원으로 처리하게 됩니다.

// MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL을 통해 연속된 메모리에 할당하는 것을 강제합니다.
// MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL이렇게 같은 속성으로 할당시키면, 다른 애들이랑 엄청 딱 붙어서 할당됩니다. 오버플로우 나면 다른 영역이 덮어쓰기 됨.
// tensor_arena를 먼저 할당하고, model_data를 할당했었는데, tensor_arena에서 오버플로우 나서 model_data가 망가졌던 적이 있습니다.

uint8_t* tensor_arena = nullptr;
uint8_t* model_data = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

// 큐에 전송할 데이터 구조체 정의
typedef struct {
    size_t length;                // 실제 읽힌 바이트 수
    uint8_t data[DMA_BUFFER_SIZE]; // 오디오 데이터
} audio_block_t;

QueueHandle_t audio_queue;
unsigned char md5sum[16];
char md5_str[33]; // 16 바이트 * 2 문자 + 널 문자
size_t model_size = 0; // 모델 크기를 저장할 전역 변수

// SPIFFS 초기화
void spiffs_init() {
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    // SPIFFS 파티션을 등록하여 ESP32의 Virtual File System(VFS)에 통합
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

// 모델 로드
const tflite::Model* load_model_from_spiffs(const char* model_path) {
    FILE* file = fopen(model_path, "rb"); // 파일 열기. FILE* 형식의 파일 포인터 반환
    if (!file) {
        ESP_LOGE(TAG, "Failed to open model file.");
        return nullptr;
    }
    
    // 파일 포인터는 파일 내에서 읽기 또는 쓰기 위치를 가리키는 포인터
    fseek(file, 0, SEEK_END); // 파일 끝으로 파일 포인터 이동
    model_size = ftell(file); // 현재 파일 포인터 위치 확인(파일 크기)
    rewind(file); // 파일의 시작으로 파일 포인터 이동

    // 모델 크기가 제한을 초과하면 오류 처리
    if (model_size > MAX_MODEL_SIZE) {
        ESP_LOGE(TAG, "Model size exceeds maximum limit of %d bytes.", MAX_MODEL_SIZE);
        fclose(file);
        return nullptr;
    }

    size_t read_size = fread(model_data, 1, model_size, file); // 데이터 저장할 메모리 주소, 읽어올 단위 데이터 크기, 읽어올 데이터 개수, 읽어올 파일 핸들들
    fclose(file);

    if (read_size != model_size) {
        ESP_LOGE(TAG, "Failed to read the complete model file. Read: %zu bytes, Expected: %zu bytes", read_size, model_size);
        return nullptr;
    }

    ESP_LOGI(TAG, "Model loaded successfully. Size: %zu bytes", model_size);
    return tflite::GetModel(model_data); // 모델 데이터를 tensorflow lite 구조체로 변환하고 반환한다.
}

// I2S 초기화
void i2s_init(i2s_chan_handle_t* i2s_rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM, // I2S 채널 선택
        .role = I2S_ROLE_MASTER, // I2S 인터페이스의 역할 지정
        .dma_desc_num = DMA_BUFFER_COUNT, // DMA 버퍼 수 지정
        .dma_frame_num = DMA_FRAME_NUM, // 각 DMA 버퍼에 저장할 샘플 프레임의 개수를 지정. 우리는 프레임 1개에 16비트임. 
        .auto_clear = true // DMA 버퍼를 자동으로 초기화
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, i2s_rx_channel)); // I2S 채널 생성. 송신, 수신 채널을 설정하고 I2S 인터페이스와 연결.

    i2s_std_config_t std_cfg = {
        .clk_cfg = { // 클럭 설정
            .sample_rate_hz = SAMPLE_RATE, // I2S 인터페이스의 샘플링 속도 설정 
            .clk_src = I2S_CLK_SRC_DEFAULT, // I2S 클럭 소스 지정
            .mclk_multiple = I2S_MCLK_MULTIPLE_256 // 마스터 클럭이 샘플링 속도의 몇 배인지 지정
        },
        .slot_cfg = { // 데이터 슬롯 설정
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, // 오디오 데이터의 비트 크기 지정
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT, // 슬롯의 비트 크기 지정
            .slot_mode = I2S_SLOT_MODE_MONO, // I2S 슬롯 모드 설정
            .slot_mask = I2S_STD_SLOT_LEFT // 활성화할 슬롯 지정
        },
        .gpio_cfg = { // GPIO 핀 설정
            .bclk = GPIO_NUM_14, // 비트 타이밍 동기화하는 핀
            .ws = GPIO_NUM_15, // 샘플의 왼쪽/오른쪽 채널 구분하는 핀
            .dout = I2S_GPIO_UNUSED, // 데이터 송신을 위한 핀. 송신을 사용하지 않음.
            .din = GPIO_NUM_32 // 데이터 수신을 위한 핀.
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*i2s_rx_channel, &std_cfg)); // I2S 채널을 표준 모드로 초기화
    ESP_ERROR_CHECK(i2s_channel_enable(*i2s_rx_channel)); // 초기화된 I2S 채널을 활성화
    ESP_LOGI(TAG, "I2S initialized successfully.");
}


// TensorFlow Lite Micro 초기화
void tflm_init() {
    // 모델 로드
    const tflite::Model* model = load_model_from_spiffs("/spiffs/wake_word_model.tflite");
    
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Failed to load model or schema version mismatch!");
        return;
    }

    // 모델 데이터의 무결성 확인
    mbedtls_md5(model_data, model_size, md5sum);
    for (int i = 0; i < 16; i++) {
        sprintf(md5_str + i * 2, "%02x", md5sum[i]); // 각 바이트를 2자리 16진수로 변환
    }
    ESP_LOGI(TAG, "MD5 of model data (loaded): %s", md5_str);

    // 필요한 연산자만 등록
    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddMaxPool2D();  // MaxPool2D 추가
    resolver.AddFullyConnected();
    resolver.AddSoftmax();

    // static으로 선언해서, 함수가 종료되어도 메모리에 interpreter가 남아있도록 함.
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, TENSOR_ARENA_SIZE, nullptr);
    interpreter = &static_interpreter;
    
    // 텐서 할당. 초기화 과정에서 모델의 구조(입력/출력 텐서, 연산자 연결 등)에 따라 텐서 메모리를 준비합니다.
    TfLiteStatus status = interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors!");
        return; // 정적 메모리는 해제 불필요
    }

    // 입력 및 출력 텐서 유효성 검증 및 상세 로그
    input_tensor = interpreter->input(0); // TfLiteTensor* 타입
    output_tensor = interpreter->output(0);
    ESP_LOGI(TAG, "Arena used bytes: %" PRIu32, static_cast<uint32_t>(interpreter->arena_used_bytes()));
    ESP_LOGI(TAG, "model_data start: %p, end: %p", model_data, model_data + model_size);
    ESP_LOGI(TAG, "tensor_arena start: %p, end: %p", tensor_arena, tensor_arena + TENSOR_ARENA_SIZE);

    // 입력 텐서 데이터 확인
    ESP_LOGI(TAG, "Input tensor info:");
    ESP_LOGI(TAG, "Tensor dims count: %d", input_tensor->dims->size);
    ESP_LOGI(TAG, "Tensor dim[0]: %d", input_tensor->dims->data[0]); // batch size
    ESP_LOGI(TAG, "Tensor dim[1]: %d", input_tensor->dims->data[1]); // feature size
    ESP_LOGI(TAG, "Tensor type: %d", input_tensor->type); // kTfLiteInt8이어야 함
}

// int16 -> int8 변환 함수
void convert_int16_to_int8(const uint8_t* src, int8_t* dest, size_t byte_len, int* sample_index) {
    // byte_len == 2 * 샘플 수 (16bit)
    // 따라서 sample_count = byte_len / 2
    // int16_t 범위(-32768 ~ 32767) -> int8_t 범위(-128 ~ 127)
    // 여기서는 단순히 1/256 스케일링
    size_t sample_count = byte_len / sizeof(int16_t);
    if (*sample_index + sample_count > INPUT_SAMPLES) {
        ESP_LOGE(TAG, "Buffer overflow imminent! sample_index=%d sample_count=%d", *sample_index, sample_count);
        sample_count = INPUT_SAMPLES - (*sample_index);
    }

    // src는 uint8_t로 받았지만, 실제로는 16비트(2바이트)씩 끊어 int16_t로 해석해야 함
    const int16_t* src_int16 = reinterpret_cast<const int16_t*>(src);

    for (size_t i = 0; i < sample_count; i++) {
        int16_t val_16 = src_int16[i];
        // 1/256로 줄여서 int8 캐스팅
        int8_t val_8 = static_cast<int8_t>(val_16 / 256);
        dest[(*sample_index)++] = val_8;
    }
}

// 오디오 캡처 태스크
void audio_capture_task(void* arg) {
    ESP_LOGI(TAG, "audio_capture_task entered");

    if (!audio_queue) {
        ESP_LOGE(TAG, "audio_queue is NULL");
        vTaskDelete(nullptr);
    }

    if (!arg) {
        ESP_LOGE(TAG, "I2S channel handle is NULL");
        vTaskDelete(nullptr);
    }
    
    i2s_chan_handle_t* i2s_rx_channel = (i2s_chan_handle_t*)arg;
    size_t bytes_read = 0;

    while (true) {
        // 동적 메모리 할당
        audio_block_t* block = (audio_block_t*)heap_caps_malloc(sizeof(audio_block_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (!block) {
            ESP_LOGE(TAG, "Failed to allocate memory for block. Free heap: %" PRIu32 ", Minimum heap: %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
            vTaskDelay(pdMS_TO_TICKS(100)); // 잠시 대기 후 재시도
            continue;
        } else {
            ESP_LOGI(TAG, "Memory allocated successfully. Block address: %p, Free heap: %" PRIu32, block, esp_get_free_heap_size());
        }

        if (!i2s_rx_channel || !(*i2s_rx_channel)) {
            ESP_LOGE(TAG, "I2S channel handle is invalid!");
            continue;
        }
        
        memset(block->data, 0, sizeof(block->data)); // 데이터 초기화
        block->length = 0; // 길이 초기화

        ESP_LOGI(TAG, "Calling i2s_channel_read...");
        esp_err_t err = i2s_channel_read(*i2s_rx_channel, block->data, DMA_BUFFER_SIZE, &bytes_read, portMAX_DELAY);

        if (err == ESP_OK) {
            block->length = bytes_read;
            if (xQueueSend(audio_queue, &block, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "%d bytes sent", bytes_read);
            } else {
                ESP_LOGE(TAG, "Failed to send data to queue.");
                heap_caps_free(block); // 전송 실패 시 메모리 해제
            }
        } else {
            ESP_LOGE(TAG, "i2s_channel_read failed with error: %s", esp_err_to_name(err));
            heap_caps_free(block); // 전송 실패 시 메모리 해제
            block = nullptr; // 포인터 초기화
        }
    }
}

// 모델 추론 태스크
void model_inference_task(void* arg) {
    ESP_LOGI(TAG, "model_inference_task entered");

    if (!audio_queue) {
        ESP_LOGE(TAG, "audio_queue is NULL");
        vTaskDelete(nullptr);
    }

    if (input_tensor == nullptr || output_tensor == nullptr) {
        ESP_LOGE(TAG, "input_tensor or output_tensor is NULL. Ensure tflm_init() initializes correctly.");
        vTaskDelete(nullptr);
    }

    int8_t* s_input_buffer = (int8_t*)heap_caps_malloc(INPUT_SAMPLES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!s_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate converted_buffer. Free heap: %" PRIu32, esp_get_free_heap_size());
        vTaskDelete(nullptr);
    }

    int sample_index = 0;

    while (true) {
        audio_block_t* recv_block = nullptr;
        ESP_LOGI(TAG, "Arena used bytes: %" PRIu32, static_cast<uint32_t>(interpreter->arena_used_bytes()));

        if (xQueueReceive(audio_queue, &recv_block, portMAX_DELAY) == pdTRUE) {
            size_t block_len = recv_block->length;
            ESP_LOGI(TAG, "Received block, length: %zu bytes", block_len);
            
            convert_int16_to_int8(recv_block->data, s_input_buffer, block_len, &sample_index);
            ESP_LOGI(TAG, "convert_int16_to_int8 ok");

            // 블록 메모리 해제
            heap_caps_free(recv_block);
            recv_block = nullptr;
            
            // 버퍼가 채워지면 추론
            if (sample_index >= INPUT_SAMPLES) {
                ESP_LOGI(TAG, "Model inference Start");
                
                // 오버플로 방지
                sample_index = INPUT_SAMPLES;

                // 모델 데이터의 무결성 확인
                mbedtls_md5(model_data, model_size, md5sum);
                for (int i = 0; i < 16; i++) {
                    sprintf(md5_str + i * 2, "%02x", md5sum[i]); // 각 바이트를 2자리 16진수로 변환
                }
                ESP_LOGI(TAG, "MD5 of model data (loaded): %s", md5_str);
                
                // TFLM 입력 텐서로 복사
                memcpy(input_tensor->data.int8, s_input_buffer, INPUT_SAMPLES);
                
                // 모델 데이터의 무결성 확인
                mbedtls_md5(model_data, model_size, md5sum);
                for (int i = 0; i < 16; i++) {
                    sprintf(md5_str + i * 2, "%02x", md5sum[i]); // 각 바이트를 2자리 16진수로 변환
                }
                ESP_LOGI(TAG, "MD5 of model data (loaded): %s", md5_str);

                // 모델 추론
                if (interpreter->Invoke() == kTfLiteOk) {
                    ESP_LOGI(TAG, "Inference OK");
                    int num_classes = output_tensor->dims->data[1];
                    for (int j = 0; j < num_classes; j++) {
                        float score = (output_tensor->data.int8[j] - output_tensor->params.zero_point)
                                      * output_tensor->params.scale;
                        ESP_LOGI(TAG, "Output[%d]: %f", j, score);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to invoke interpreter.");
                }

                // 다음 추론 위해 sample_index 초기화
                sample_index = 0;
            }
        } else {
            ESP_LOGE(TAG, "Failed to receive data from queue.");
        }
    }
}

// 메인 함수
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Largest free block (8-bit memory): %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Heap free size (8-bit memory): %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    // 동적 메모리 할당
    model_data = (uint8_t*)heap_caps_aligned_alloc(16, MAX_MODEL_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL); // MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL
    if (!model_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for model_data. Free heap: %" PRIu32, esp_get_free_heap_size());
        esp_restart(); // 메모리 부족 시 재부팅
    }

    ESP_LOGI(TAG, "Largest free block (8-bit memory): %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Heap free size (8-bit memory): %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    tensor_arena = (uint8_t*)heap_caps_aligned_alloc(16, TENSOR_ARENA_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL); // MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL를 통해서 연속적으로 메모리 할당당
    if (!tensor_arena) {
        ESP_LOGE(TAG, "Failed to allocate memory for tensor_arena. Free heap: %" PRIu32, esp_get_free_heap_size());
        esp_restart(); // 메모리 부족 시 재부팅
    }

    ESP_LOGI(TAG, "Largest free block (8-bit memory): %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Heap free size (8-bit memory): %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    ESP_LOGI(TAG, "Memory allocated successfully. Tensor Arena: %p, Model Data: %p", tensor_arena, model_data);

    esp_log_level_set("*", ESP_LOG_VERBOSE);

    static i2s_chan_handle_t i2s_rx_channel = nullptr;
    spiffs_init();
    i2s_init(&i2s_rx_channel);
    tflm_init();

    // 큐 생성: audio_block_t* 포인터 10개 담는 크기로 설정
    audio_queue = xQueueCreate(10, sizeof(audio_block_t*));
    if (!audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio_queue. Restarting...");
        esp_restart();
    }

    // 태스크 생성: 스택 크기를 충분히 할당하고, 우선순위와 코어를 설정
    xTaskCreatePinnedToCore(audio_capture_task, "Audio Capture", 4096, &i2s_rx_channel, 5, nullptr, 0);
    xTaskCreatePinnedToCore(model_inference_task, "Model Inference", 8192, nullptr, 6, nullptr, 0);
}