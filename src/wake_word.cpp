#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"  // 필요한 연산자만 등록할 수 있음.
#include "tensorflow/lite/micro/micro_interpreter.h" // TensorFlow Lite Micro 인터프리터를 정의하는 헤더 파일. 모델 데이터를 실행하고, 입력/출력 텐서를 관리함.
#include "tensorflow/lite/schema/schema_generated.h" // TensorFlow Lite 모델의 스키마 정의를 포함하는 헤더 파일. 모델의 버전 및 구조를 확인함.
#include "tensorflow/lite/micro/micro_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h" // I2S (마이크 입력 처리)를 위한 ESP32 드라이버.
#include "esp_log.h"  // ESP32 로깅 유틸리티.
#include "esp_system.h" // ESP32 시스템 관련 유틸리티.
#include "esp_spiffs.h"
#include <iostream>
#include <vector>
#include <inttypes.h> // PRIu32 매크로를 사용하기 위해 필요합니다.
#include <cmath>

#define I2S_NUM         I2S_NUM_0
#define DMA_BUFFER_COUNT 8
#define I2S_BUFFER_SIZE 4000
#define SAMPLE_RATE     4000  // 입력 데이터 샘플링 속도 4000Hz로 설정
#define INPUT_SIZE      (SAMPLE_RATE * 2)  // 바이트 단위 크기(1샘플이 2바이트)
#define INPUT_SAMPLES   SAMPLE_RATE        // 1초에 4000 샘플 (16-bit PCM)
#define TENSOR_ARENA_SIZE 14 * 1024
#define MAX_MODEL_SIZE 4 * 1024

static const char *TAG = "WAKE_WORD";

// TensorFlow Lite Micro(TFLM)에서 텐서 및 중간 계산 데이터를 저장하기 위한 메모리 공간.
__attribute__((aligned(16))) static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

// 플래시 메모리(SPIFFS)에서 읽어들인 모델 바이너리 데이터를 저장.
__attribute__((aligned(16))) static uint8_t model_data[MAX_MODEL_SIZE];

// TensorFlow Lite Micro 인터프리터 및 텐서 포인터
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

QueueHandle_t audio_queue;

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
    size_t model_size = ftell(file); // 현재 파일 포인터 위치 확인(파일 크기)
    rewind(file); // 파일의 시작으로 파일 포인터 이동

    // 모델 크기가 제한을 초과하면 오류 처리
    if (model_size > MAX_MODEL_SIZE) {
        ESP_LOGE(TAG, "Model size exceeds maximum limit of %d bytes.", MAX_MODEL_SIZE);
        fclose(file);
        return nullptr;
    }

    fread(model_data, 1, model_size, file); // 데이터를 저장할 메모리의 시작 주소, 읽을 단위 데이터의 크기(바이트 단위), 읽을 데이터의 개수, 파일을 읽을 파일 포인터
    fclose(file); // 파일 닫기. 닫을 파일 포인터를 인자로 넘긴다.

    ESP_LOGI(TAG, "Model loaded successfully. Size: %zu bytes", model_size);
    return tflite::GetModel(model_data);
}

// I2S 초기화
void i2s_init(i2s_chan_handle_t* i2s_rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM, // I2S 채널 선택
        .role = I2S_ROLE_MASTER, // I2S 인터페이스의 역할 지정
        .dma_desc_num = DMA_BUFFER_COUNT, // DMA 버퍼 수 지정
        .dma_frame_num = I2S_BUFFER_SIZE / DMA_BUFFER_COUNT, // 각 DMA 버퍼에 저장할 샘플 프레임의 개수를 지정
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
    ESP_ERROR_CHECK(i2s_channel_enable(*i2s_rx_channel)); // 초기화된 I2S 채널을 활성화화
    ESP_LOGI(TAG, "I2S initialized successfully.");
}


// TensorFlow Lite Micro 초기화
void tflm_init() {
    // 모델 로드
    const tflite::Model* model = load_model_from_spiffs("/spiffs/wake_word_model.tflite");
    if (!model) {
        ESP_LOGE(TAG, "Failed to load the model from SPIFFS.");
        return;
    }

    // 필요한 연산자만 등록
    static tflite::MicroMutableOpResolver<8> resolver;
    resolver.AddShape();
    resolver.AddStridedSlice();
    resolver.AddPack();
    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddMean();
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

    // Tensor Arena 사용량 추정 로그
    uint8_t* tensor_arena_end = tensor_arena + TENSOR_ARENA_SIZE;
    uint8_t* last_used_address = reinterpret_cast<uint8_t*>(static_interpreter.input(0));  // 임의 기준으로 사용
    size_t used_bytes = tensor_arena_end - last_used_address;
    ESP_LOGI(TAG, "Tensor Arena Usage: %zu / %zu bytes", used_bytes, TENSOR_ARENA_SIZE);

    input_tensor = interpreter->input(0); // TfLiteTensor* 타입 
    output_tensor = interpreter->output(0);
}

// 오디오 캡처 태스크
void audio_capture_task(void* arg) {
    ESP_LOGI(TAG, "audio_capture_task Stack watermark: %d bytes", uxTaskGetStackHighWaterMark(nullptr));

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    if (!audio_queue) {
        ESP_LOGI(TAG, "null audio_queue");
        vTaskDelete(nullptr);
    }

    // 크기가 큰 배열을 힙으로 할당
    i2s_chan_handle_t* i2s_rx_channel = (i2s_chan_handle_t*)arg;
    uint8_t* current_buffer = (uint8_t*)heap_caps_malloc(I2S_BUFFER_SIZE, MALLOC_CAP_DMA);
    if (!current_buffer) {
        ESP_LOGE(TAG, "Failed to allocate current_buffer.");
        vTaskDelete(nullptr);
    }
    
    size_t bytes_read = 0;

    while (true) {
        ESP_LOGE(TAG, "before I2S_channel_read");
        esp_err_t err = i2s_channel_read(*i2s_rx_channel, current_buffer, I2S_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
        ESP_LOGE(TAG, "after I2S_channel_read");
        if (err != ESP_OK) continue;

        if (xQueueSend(audio_queue, current_buffer, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send data to queue.");
        }
    }
    // 리소스 해제
    heap_caps_free(current_buffer);\
}

// 모델 추론 태스크
void model_inference_task(void* arg) {
    ESP_LOGI(TAG, "model_inference_task Stack watermark: %d bytes", uxTaskGetStackHighWaterMark(nullptr));

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    if (!audio_queue) {
        ESP_LOGI(TAG, "null audio_queue");
        vTaskDelete(nullptr);
    }

    // uint8_t 데이터를 저장할 임시 버퍼와 최종 int16_t 데이터 버퍼
    uint8_t* queue_buffer = (uint8_t*)heap_caps_malloc(I2S_BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    if (!queue_buffer) {
        ESP_LOGE(TAG, "Failed to allocate queue_buffer.");
        vTaskDelete(nullptr);
    }

    int16_t* samples = (int16_t*)heap_caps_malloc(INPUT_SAMPLES * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    if (!samples) {
        ESP_LOGE(TAG, "Failed to allocate samples.");
        heap_caps_free(queue_buffer);
        vTaskDelete(nullptr);
    }

    int sample_index = 0;

    while (true) {
        // 큐에서 uint8_t 데이터를 읽어오기
        if (xQueueReceive(audio_queue, queue_buffer, portMAX_DELAY) == pdTRUE) {
            size_t bytes_to_process = I2S_BUFFER_SIZE / 2; // 16-bit PCM 기준
            for (size_t i = 0; i < bytes_to_process; i++) {
                samples[sample_index++] = (int16_t)((queue_buffer[2 * i + 1] << 8) | queue_buffer[2 * i]);

                // INPUT_SAMPLES만큼 데이터가 모이면 추론 실행
                if (sample_index == INPUT_SAMPLES) {
                    int8_t* input_data = input_tensor->data.int8;
                    for (int j = 0; j < INPUT_SAMPLES; j++) {
                        input_data[j] = (samples[j] / 32768.0f) * input_tensor->params.scale + input_tensor->params.zero_point;
                    }

                    // 모델 추론
                    if (interpreter->Invoke() == kTfLiteOk) {
                        for (int j = 0; j < output_tensor->dims->data[1]; j++) {
                            float score = (output_tensor->data.int8[j] - output_tensor->params.zero_point) * output_tensor->params.scale;
                            ESP_LOGI(TAG, "Output %d: %f", j, score);
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to invoke interpreter.");
                    }

                    // 인덱스 초기화
                    sample_index = 0;
                }
            }
        }
    }
    // 리소스 해제
    heap_caps_free(samples);
}

// 메인 함수
extern "C" void app_main(void) {
    i2s_chan_handle_t i2s_rx_channel = nullptr;
    spiffs_init();
    i2s_init(&i2s_rx_channel);
    tflm_init();

    audio_queue = xQueueCreate(5, sizeof(int8_t) * I2S_BUFFER_SIZE);
    if (!audio_queue) esp_restart();

    xTaskCreatePinnedToCore(audio_capture_task, "Audio Capture", 40960, &i2s_rx_channel, 10, nullptr, 0);
    xTaskCreatePinnedToCore(model_inference_task, "Model Inference", 40960, nullptr, 5, nullptr, 1);
}