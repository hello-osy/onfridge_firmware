#include "tflite_model.h"  // 변환된 헤더 파일
#include "tensorflow/lite/micro/all_ops_resolver.h"  // TensorFlow Lite Micro에서 지원하는 모든 연산자(op)를 등록함.
#include "tensorflow/lite/micro/micro_interpreter.h" // TensorFlow Lite Micro 인터프리터를 정의하는 헤더 파일. 모델 데이터를 실행하고, 입력/출력 텐서를 관리함.
#include "tensorflow/lite/schema/schema_generated.h" // TensorFlow Lite 모델의 스키마 정의를 포함하는 헤더 파일. 모델의 버전 및 구조를 확인함.

#include "driver/i2s_std.h" // I2S (마이크 입력 처리)를 위한 ESP32 드라이버.
#include "esp_log.h"  // ESP32 로깅 유틸리티.
#include "esp_system.h" // ESP32 시스템 관련 유틸리티.

#define I2S_NUM         I2S_NUM_0
#define SAMPLE_RATE     16000
#define TENSOR_ARENA_SIZE 10240  // TENSOR_ARENA_SIZE->모델 실행에 필요한 메모리 공간 크기(바이트 단위)

static const char *TAG = "INMP441_TFLM"; // 로깅 시 표시될 태그를 정의합니다. 디버깅 및 로깅 메시지 구분에 사용됩니다.

// TensorFlow Lite Micro 설정
uint8_t tensor_arena[TENSOR_ARENA_SIZE]; // tensor_arena->모델 실행을 위한 메모리 버퍼. TensorFlow Lite Micro 인터프리터는 이 버퍼를 사용하여 중간 데이터, 가중치 등을 저장함.
tflite::MicroInterpreter* interpreter; // TensorFlow Lite Micro 인터프리터 객체.
TfLiteTensor* input_tensor; // 모델의 입력 데이터를 저장하는 텐서.
TfLiteTensor* output_tensor; // 모델의 출력 데이터를 저장하는 텐서.

// I2S 초기화
void i2s_init(i2s_chan_handle_t *i2s_rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 2,
        .dma_frame_num = 512,
        .auto_clear = true
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, i2s_rx_channel));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT
        },
        .gpio_cfg = {
            .bclk = 14,  // BCLK 핀
            .ws = 15,    // WS 핀
            .dout = I2S_GPIO_UNUSED,
            .din = 32    // INMP441 SD 핀
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*i2s_rx_channel, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*i2s_rx_channel));
    ESP_LOGI(TAG, "I2S initialized successfully.");
}

// TensorFlow Lite Micro 초기화
void tflm_init() {
    const tflite::Model* model = tflite::GetModel(model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version does not match!");
        return;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, TENSOR_ARENA_SIZE, nullptr);
    interpreter = &static_interpreter;

    // 모델 초기화
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors!");
        return;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    ESP_LOGI(TAG, "TensorFlow Lite Micro initialized successfully.");
}

// I2S 데이터 처리 및 모델 실행
void process_audio(i2s_chan_handle_t i2s_rx_channel) {
    uint8_t audio_buffer[512];
    size_t bytes_read;

    ESP_LOGI(TAG, "Processing audio...");
    while (1) {
        // I2S 데이터 읽기
        ESP_ERROR_CHECK(i2s_channel_read(i2s_rx_channel, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY));

        // 입력 텐서에 데이터 복사
        for (size_t i = 0; i < bytes_read / 2; i++) {
            input_tensor->data.f[i] = ((int16_t*)audio_buffer)[i] / 32768.0f;  // 정규화
        }

        // 모델 실행
        if (interpreter->Invoke() != kTfLiteOk) {
            ESP_LOGE(TAG, "Failed to invoke TFLite model!");
            continue;
        }

        // 출력 결과 확인
        float result = output_tensor->data.f[0];  // 예측 결과
        ESP_LOGI(TAG, "Inference result: %f", result);

        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기
    }
}

void app_main() {
    i2s_chan_handle_t i2s_rx_channel;

    // I2S 및 TensorFlow Lite Micro 초기화
    i2s_init(&i2s_rx_channel);
    tflm_init();

    // 오디오 데이터 처리
    process_audio(i2s_rx_channel);
}