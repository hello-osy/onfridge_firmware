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

#define I2S_NUM         I2S_NUM_0
#define SAMPLE_RATE     16000
#define DMA_BUFFER_COUNT 2
#define I2S_BUFFER_SIZE 4000
#define TENSOR_ARENA_SIZE 150 * 1024
#define INPUT_SIZE      16000  // 모델의 입력 크기 (1초)
static const char *TAG = "WAKE_WORD"; // 로깅 시 표시될 태그를 정의합니다. 디버깅 및 로깅 메시지 구분에 사용됩니다.


// model_tflite는 플래시 메모리에 spiffs로 저장합니다.
// .map 파일은 컴파일러와 링커가 생성하는 파일로, 프로그램의 메모리 배치와 관련된 중요한 정보를 제공합니다.
// ESP32와 같은 임베디드 시스템에서는 특히 .map 파일을 분석하여 메모리 사용, 플래시 메모리 배치, 그리고 디버깅 문제를 해결하는 데 유용합니다.

// SRAM(Static RAM): ESP32에서 데이터 처리를 위한 RAM으로, IRAM과 DRAM으로 구분됩니다.
//                   IRAM은 주로 실행 코드(.text)에 사용되고, DRAM은 데이터(.data, .bss, .heap, .stack)에 사용됩니다.
//                   프로그램 실행 중 동적 데이터와 스택 메모리를 관리합니다.

// IRAM(Instruction RAM): SRAM의 일부로, 실행 가능한 코드가 저장되는 고속 RAM 영역입니다. 
//                        CPU가 직접 접근하여 실행 속도가 빠릅니다.
//                        인터럽트 핸들러와 같은 고성능 코드가 여기에 배치될 수 있으며, 일부 읽기 전용 데이터도 배치 가능합니다.

// DRAM(Data RAM): SRAM의 또 다른 부분으로, 주로 데이터 처리를 위해 사용됩니다. 
//                 전역 변수(.data, .bss), 동적 메모리 할당(.heap), 스택(.stack)이 DRAM에 배치됩니다.
//                 DRAM은 우리가 일반적으로 사용하는 DDR DRAM과는 다릅니다.
//                 DDR DRAM은 외부 메모리로 고속 버스와 함께 사용되지만, ESP32의 DRAM은 내부 SRAM으로 구현되어 있습니다.
//                 따라서 DRAM은 정적 RAM(SRAM)으로 작동하며, 휘발성 메모리로 전원이 꺼지면 데이터가 손실됩니다.

// 플래시(Flash): 비휘발성 메모리로, ESP32의 프로그램 코드(.text), 읽기 전용 데이터(.rodata), 초기화된 전역 변수(.data)가 저장됩니다.
//               실행 시 .text는 IRAM으로, .data는 DRAM으로 복사되어 실행됩니다.
//               플래시는 전원이 꺼져도 데이터가 유지되며, 읽기 속도는 RAM보다 느리지만, 데이터 저장 용도로 적합합니다.

// .text: 실행 가능한 명령어 (코드 섹션).               -> 플래시 메모리 (IRAM으로 복사되어 실행)
// .rodata: 읽기 전용 데이터 (상수, 문자열 등).         -> 플래시 메모리
// .data: 초기화된 전역 변수.                          -> SRAM (실행 시 DRAM 또는 IRAM에 배치 가능)
// .bss: 초기화되지 않은 전역 변수.                     -> SRAM (DRAM에 배치)
// .heap: 동적 메모리 할당 영역 (malloc, new 등).       -> SRAM (DRAM에 배치)
// .stack: 함수 호출 시 스택 프레임 및 로컬 변수 저장.    -> SRAM (DRAM에 배치, 태스크별로 독립 관리)

// **참고**:
// ESP32의 DRAM은 내부 SRAM이며, DDR DRAM과는 다릅니다. DDR DRAM은 외부에서 추가로 장착해야 하지만,
// ESP32의 DRAM은 칩 내부에 통합되어 있습니다. 이는 임베디드 시스템에서의 메모리 접근 속도와 비용 효율성을 극대화하기 위해 설계되었습니다.


// TensorFlow Lite Micro 설정
uint8_t tensor_arena[TENSOR_ARENA_SIZE];
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

// SPIFFS 초기화
void spiffs_init() {
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
    } else {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information.");
    }
}

// 모델 로드
const tflite::Model* load_model_from_spiffs(const char* model_path, size_t* model_size) {
    ESP_LOGI(TAG, "Loading model from SPIFFS: %s", model_path);

    FILE* file = fopen(model_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open model file.");
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *model_size = ftell(file); // 모델 크기 저장
    rewind(file);

    uint8_t* model_data = static_cast<uint8_t*>(malloc(*model_size));
    if (!model_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for model.");
        fclose(file);
        return nullptr;
    }

    fread(model_data, 1, *model_size, file);
    fclose(file);

    ESP_LOGI(TAG, "Model loaded successfully. Size: %zu bytes", *model_size);
    return tflite::GetModel(model_data);
}

// I2S 초기화
void i2s_init(i2s_chan_handle_t* i2s_rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = DMA_BUFFER_COUNT,
        .dma_frame_num = I2S_BUFFER_SIZE / DMA_BUFFER_COUNT,
        .auto_clear = true
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, i2s_rx_channel));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT
        },
        .gpio_cfg = {
            .bclk = GPIO_NUM_14,
            .ws = GPIO_NUM_15,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_32
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*i2s_rx_channel, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*i2s_rx_channel));
    ESP_LOGI(TAG, "I2S initialized successfully.");
}

// TensorFlow Lite Micro 초기화
void tflm_init() {
    ESP_LOGI(TAG, "Initializing TensorFlow Lite Micro...");

    size_t model_size = 0; // 모델 크기를 저장할 변수
    const tflite::Model* model = load_model_from_spiffs("/spiffs/wake_word_model.tflite", &model_size);

    if (!model) {
        ESP_LOGE(TAG, "Model is null! Please check the model file.");
        return;
    }

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version mismatch!");
        return;
    }

    ESP_LOGI(TAG, "Model version: %" PRIu32, model->version());
    ESP_LOGI(TAG, "Model size: %zu bytes", model_size);

    // 필요한 연산자만 등록
    static tflite::MicroMutableOpResolver<11> resolver;
    resolver.AddShape();
    resolver.AddStridedSlice();
    resolver.AddPack();
    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddMean();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, TENSOR_ARENA_SIZE, nullptr);
    interpreter = &static_interpreter;

    ESP_LOGI(TAG, "Tensor arena size: %d bytes", TENSOR_ARENA_SIZE);
    ESP_LOGI(TAG, "Free heap size before tensor allocation: %" PRIu32 " bytes", esp_get_free_heap_size());

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors!");
        ESP_LOGI(TAG, "Tensor arena size: %d bytes", TENSOR_ARENA_SIZE);
        ESP_LOGI(TAG, "Free heap size: %" PRIu32 " bytes", esp_get_free_heap_size());
        return;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    ESP_LOGI(TAG, "Tensor allocation successful.");
    ESP_LOGI(TAG, "Tensor arena used: %zu bytes", interpreter->arena_used_bytes());

    if (!input_tensor || !output_tensor) {
        ESP_LOGE(TAG, "Tensor initialization failed: Input or Output tensor is null.");
        return;
    }
}

// I2S 데이터 처리 및 모델 실행
void process_audio(i2s_chan_handle_t i2s_rx_channel) {
    // 두 개의 버퍼 할당
    std::vector<int16_t> buffer_a(INPUT_SIZE);
    std::vector<int16_t> buffer_b(INPUT_SIZE);

    int16_t* current_buffer = buffer_a.data();
    int16_t* processing_buffer = buffer_b.data();

    while (true) {
        size_t bytes_read = 0;

        // I2S에서 현재 버퍼로 데이터 읽기
        ESP_ERROR_CHECK(i2s_channel_read(i2s_rx_channel, current_buffer, INPUT_SIZE * sizeof(int16_t), &bytes_read, portMAX_DELAY));

        if (bytes_read != INPUT_SIZE * sizeof(int16_t)) {
            ESP_LOGW(TAG, "Incomplete audio data read: expected %d bytes, got %d bytes.", INPUT_SIZE * sizeof(int16_t), bytes_read);
            continue;
        }

        // 모델에 처리할 데이터를 복사
        for (size_t i = 0; i < INPUT_SIZE; ++i) {
            input_tensor->data.f[i] = static_cast<float>(processing_buffer[i]) / 32768.0f;
        }

        // 모델 실행
        if (interpreter->Invoke() != kTfLiteOk) {
            ESP_LOGE(TAG, "Failed to invoke TensorFlow Lite model.");
        } else {
            // 결과 확인
            int wake_word_detected = -1;
            float max_score = -1.0f;

            for (int i = 0; i < output_tensor->dims->data[1]; ++i) {
                float score = output_tensor->data.f[i];
                ESP_LOGI(TAG, "Output %d: %f", i, score);
                if (score > max_score) {
                    max_score = score;
                    wake_word_detected = i;
                }
            }

            if (wake_word_detected == 1) { // 1이 웨이크 워드 감지를 나타낸다고 가정
                ESP_LOGI(TAG, "Wake word detected!");
            } else {
                ESP_LOGI(TAG, "No wake word detected.");
            }
        }

        // 버퍼 스왑
        int16_t* temp = current_buffer;
        current_buffer = processing_buffer;
        processing_buffer = temp;
    }
}

extern "C" void app_main(void) {
    i2s_chan_handle_t i2s_rx_channel = nullptr;;
    ESP_LOGI(TAG, "Application started.");
    spiffs_init();
    i2s_init(&i2s_rx_channel);
    tflm_init();
    
    if (!input_tensor || !output_tensor) {
        ESP_LOGE(TAG, "Tensor initialization failed.");
        return;
    }

    process_audio(i2s_rx_channel);
}
