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
#include <iostream>
#include <vector>
#include <inttypes.h> // PRIu32 매크로를 사용하기 위해 필요합니다.
#include <cmath>

#define I2S_NUM         I2S_NUM_0
#define DMA_BUFFER_COUNT 3
#define I2S_BUFFER_SIZE 2000
#define SAMPLE_RATE     4000  // 입력 데이터 샘플링 속도 4000Hz로 설정
#define INPUT_SIZE      (SAMPLE_RATE * 2)  // 바이트 단위 크기(1샘플이 2바이트)
#define INPUT_SAMPLES   SAMPLE_RATE        // 1초에 4000 샘플 (16-bit PCM)
#define TENSOR_ARENA_SIZE 14 * 1024
#define MAX_MODEL_SIZE 4 * 1024

static const char *TAG = "WAKE_WORD";

// 타입 주의 uint8_t, uint16_t, int8_t, int16_t 다 다릅니다. 
// 메모리 블록은 부호의 의미를 가지지 않으므로, 이를 **부호 없는 데이터(uint8_t)**로 정의하는 것이 일반적입니다.
// 나머지 오디오 관련 데이터는 부호 있는 데이터로 정의했습니다.

// uint8_t를 사용하면, 이후 이 공간을 어떤 방식으로든 해석할 수 있습니다(int8_t, float, int32_t, etc.).
// 반대로 int8_t로 정의하면, 해당 공간이 부호 있는 8비트 정수라는 의미를 갖게 되어 해석이 제한될 수 있습니다.

// TensorFlow Lite Micro(TFLM)에서 텐서 및 중간 계산 데이터를 저장하기 위한 메모리 공간.
__attribute__((aligned(16))) static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

// 플래시 메모리(SPIFFS)에서 읽어들인 모델 바이너리 데이터를 저장.
__attribute__((aligned(16))) static uint8_t model_data[MAX_MODEL_SIZE];

// TensorFlow Lite Micro 인터프리터 및 텐서 포인터
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

// 큐에 전송할 데이터 구조체 정의
typedef struct {
    size_t length;                // 실제 읽힌 바이트 수
    uint8_t data[I2S_BUFFER_SIZE]; // 오디오 데이터
} audio_block_t;

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

    input_tensor = interpreter->input(0); // TfLiteTensor* 타입 
    output_tensor = interpreter->output(0);
        
    // 입력 텐서 유효성 검증 및 상세 로그
    if (input_tensor == nullptr) {
        ESP_LOGE(TAG, "Input tensor is NULL. Model may not be initialized properly.");
        return;
    }
    ESP_LOGI(TAG, "Input tensor information:");
    ESP_LOGI(TAG, "  Type: %d (Expected: %d)", input_tensor->type, kTfLiteInt8);
    ESP_LOGI(TAG, "  Dimensions:");
    for (int i = 0; i < input_tensor->dims->size; i++) {
        ESP_LOGI(TAG, "    dim[%d]: %d", i, input_tensor->dims->data[i]);
    }
    ESP_LOGI(TAG, "  Quantization params:");
    ESP_LOGI(TAG, "    Scale: %f", input_tensor->params.scale);
    ESP_LOGI(TAG, "    Zero Point: %" PRId32, input_tensor->params.zero_point); // 수정

    // 출력 텐서 유효성 검증 및 상세 로그
    if (output_tensor == nullptr) {
        ESP_LOGE(TAG, "Output tensor is NULL. Model may not be initialized properly.");
        return;
    }
    ESP_LOGI(TAG, "Output tensor information:");
    ESP_LOGI(TAG, "  Type: %d (Expected: %d)", output_tensor->type, kTfLiteInt8);
    ESP_LOGI(TAG, "  Dimensions:");
    for (int i = 0; i < output_tensor->dims->size; i++) {
        ESP_LOGI(TAG, "    dim[%d]: %d", i, output_tensor->dims->data[i]);
    }
    ESP_LOGI(TAG, "  Quantization params:");
    ESP_LOGI(TAG, "    Scale: %f", output_tensor->params.scale);
    ESP_LOGI(TAG, "    Zero Point: %" PRId32, output_tensor->params.zero_point); // 수정

    // 입력 텐서 타입 검증
    if (input_tensor->type != kTfLiteInt8) {
        ESP_LOGE(TAG, "Input tensor type mismatch. Expected: %d, Found: %d", kTfLiteInt8, input_tensor->type);
    } else {
        ESP_LOGI(TAG, "Input tensor type is correct: %d", input_tensor->type);
    }

    // 출력 텐서 타입 검증
    if (output_tensor->type != kTfLiteInt8) {
        ESP_LOGE(TAG, "Output tensor type mismatch. Expected: %d, Found: %d", kTfLiteInt8, output_tensor->type);
    } else {
        ESP_LOGI(TAG, "Output tensor type is correct: %d", output_tensor->type);
    }

}

// 데이터를 한 번에 변환하는 함수 (비트 연산 사용)
void convert_uint8_to_int8_bitwise(uint8_t* src, int8_t* dest, size_t len, int* sample_index) {
    for (size_t i = 0; i < len; i++) {
        dest[(*sample_index)++] = (int8_t)(src[i] ^ 0x80);  // XOR 연산으로 변환
    }
}

// 오디오 캡처 태스크
void audio_capture_task(void* arg) {
    ESP_LOGI(TAG, "audio_capture_task entered");

    if (!audio_queue) {
        ESP_LOGE(TAG, "audio_queue is NULL");
        vTaskDelete(nullptr);
    }
    
    i2s_chan_handle_t* i2s_rx_channel = (i2s_chan_handle_t*)arg;
    audio_block_t block;
    size_t bytes_read = 0;

    while (true) {
        esp_err_t err = i2s_channel_read(*i2s_rx_channel, block.data, I2S_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
        if (err == ESP_OK) {
            block.length = bytes_read;
            if (xQueueSend(audio_queue, &block, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "%d bytes sent", bytes_read);
            } else {
                ESP_LOGE(TAG, "Failed to send data to queue.");
            }
        } else {
            ESP_LOGE(TAG, "I2S read failed with error: %d", err);
        }
    }

    // 리소스 해제 (실제로는 도달하지 않음)
    heap_caps_free(block.data);
}

// 모델 추론 태스크
void model_inference_task(void* arg) {
    ESP_LOGI(TAG, "model_inference_task entered");

    if (!audio_queue) {
        ESP_LOGE(TAG, "audio_queue is NULL");
        vTaskDelete(nullptr);
    }
    
    audio_block_t recv_block;

    int8_t* converted_buffer = (int8_t*)heap_caps_malloc(INPUT_SAMPLES, MALLOC_CAP_DEFAULT);
    if (!converted_buffer) {
        ESP_LOGE(TAG, "Failed to allocate converted_buffer.");
        vTaskDelete(nullptr);
    }

    int sample_index = 0;

    while (true) {
        if (xQueueReceive(audio_queue, &recv_block, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "xQueueReceive ok, bytes_received: %zu", recv_block.length);
            
            // 변환된 데이터가 버퍼에 남지 않도록 최대값 체크
            if (sample_index + recv_block.length > INPUT_SAMPLES) {
                ESP_LOGW(TAG, "Buffer overflow detected. Truncating the data.");
                recv_block.length = INPUT_SAMPLES - sample_index;
            }

            // 한 번에 변환
            convert_uint8_to_int8_bitwise(recv_block.data, converted_buffer, recv_block.length, &sample_index);
            ESP_LOGI(TAG, "convert_uint8_to_int8_bitwise ok, sample_index: %d", sample_index);

            // 버퍼가 채워지면 추론
            if (sample_index >= INPUT_SAMPLES) {
                ESP_LOGI(TAG, "Buffer full. Preparing for inference.");

                // 입력 텐서로 데이터를 채움
                int8_t* input_data = input_tensor->data.int8;
                memcpy(input_data, converted_buffer, INPUT_SAMPLES * sizeof(int8_t));

                // 모델 추론
                if (interpreter->Invoke() == kTfLiteOk) {
                    ESP_LOGI(TAG, "interpreter->Invoke() == kTfLiteOk");
                    for (int j = 0; j < output_tensor->dims->data[1]; j++) {
                        float score = (output_tensor->data.int8[j] - output_tensor->params.zero_point) * output_tensor->params.scale;
                        ESP_LOGI(TAG, "Output %d: %f", j, score);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to invoke interpreter.");
                }

                // 버퍼 초기화
                sample_index = 0;
            }
        } else {
            ESP_LOGE(TAG, "Failed to receive data from queue.");
        }
    }

    heap_caps_free(converted_buffer);
}

// 메인 함수
extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    i2s_chan_handle_t i2s_rx_channel = nullptr;
    spiffs_init();
    i2s_init(&i2s_rx_channel);
    tflm_init();

    // 큐 생성: audio_block_t 구조체 크기로 설정
    audio_queue = xQueueCreate(3, sizeof(audio_block_t));
    if (!audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio_queue. Restarting...");
        esp_restart();
    }

    // 태스크 생성: 스택 크기를 충분히 할당하고, 우선순위와 코어를 설정
    xTaskCreatePinnedToCore(audio_capture_task, "Audio Capture", 4096, &i2s_rx_channel, 10, nullptr, 0);
    xTaskCreatePinnedToCore(model_inference_task, "Model Inference", 8192, nullptr, 5, nullptr, 1);
}