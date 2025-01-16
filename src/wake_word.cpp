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
#define DMA_BUFFER_COUNT 2
#define I2S_BUFFER_SIZE 4000
#define SAMPLE_RATE     4000  // 입력 데이터 샘플링 속도 4000Hz로 설정
#define INPUT_SIZE      (SAMPLE_RATE * 2)  // 바이트 단위 크기(1샘플이 2바이트)
#define INPUT_SAMPLES   SAMPLE_RATE        // 1초에 4000 샘플 (16-bit PCM)
#define TENSOR_ARENA_SIZE 20 * 1024
#define MAX_MODEL_SIZE 20 * 1024

// TensorFlow Lite Micro(TFLM)에서 텐서 및 중간 계산 데이터를 저장하기 위한 메모리 공간.
__attribute__((aligned(16))) static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

// 플래시 메모리(SPIFFS)에서 읽어들인 모델 바이너리 데이터를 저장.
__attribute__((aligned(16))) static uint8_t model_data[MAX_MODEL_SIZE];

// TensorFlow Lite Micro 인터프리터 및 텐서 포인터
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

// 로깅 시 표시될 태그를 정의합니다. 디버깅 및 로깅 메시지 구분에 사용됩니다.
static const char *TAG = "WAKE_WORD";

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
const tflite::Model* load_model_from_spiffs(const char* model_path) {
    ESP_LOGI(TAG, "Loading model from SPIFFS: %s", model_path);

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

// 서브그래프 및 연산자 정보 출력
void print_subgraph_and_operator_info(const tflite::Model* model) {
    ESP_LOGI(TAG, "Model contains the following subgraphs and operators:");
    
    auto subgraphs = model->subgraphs();
    if (!subgraphs) {
        ESP_LOGE(TAG, "No subgraphs found in the model.");
        return;
    }

    for (int subgraph_idx = 0; subgraph_idx < subgraphs->size(); ++subgraph_idx) {
        const auto* subgraph = subgraphs->Get(subgraph_idx);
        ESP_LOGI(TAG, "Subgraph %lu:", static_cast<unsigned long>(subgraph_idx));  // %lu 사용
        ESP_LOGI(TAG, "  Number of Tensors: %lu", static_cast<unsigned long>(subgraph->tensors()->size()));
        ESP_LOGI(TAG, "  Number of Inputs: %lu", static_cast<unsigned long>(subgraph->inputs()->size()));
        ESP_LOGI(TAG, "  Number of Outputs: %lu", static_cast<unsigned long>(subgraph->outputs()->size()));
        ESP_LOGI(TAG, "  Number of Operators: %lu", static_cast<unsigned long>(subgraph->operators()->size()));

        // 입력 텐서 정보 출력
        const auto* input_indices = subgraph->inputs();
        if (input_indices && input_indices->size() > 0) {
            for (int i = 0; i < input_indices->size(); ++i) {
                int input_index = input_indices->Get(i);
                const auto* tensor = subgraph->tensors()->Get(input_index);
                ESP_LOGI(TAG, "  Input Tensor %d Dimensions:", i);

                if (tensor) {
                    const auto* dims = tensor->shape();
                    for (int dim_idx = 0; dim_idx < dims->size(); ++dim_idx) {
                        ESP_LOGI(TAG, "    Dimension %d: %ld", dim_idx, dims->Get(dim_idx)); // %ld 사용
                    }
                } else {
                    ESP_LOGE(TAG, "    Tensor information not available.");
                }
            }
        }

        // 출력 텐서 정보 출력
        const auto* output_indices = subgraph->outputs();
        if (output_indices && output_indices->size() > 0) {
            for (int i = 0; i < output_indices->size(); ++i) {
                int output_index = output_indices->Get(i);
                const auto* tensor = subgraph->tensors()->Get(output_index);
                ESP_LOGI(TAG, "  Output Tensor %d Dimensions:", i);

                if (tensor) {
                    const auto* dims = tensor->shape();
                    for (int dim_idx = 0; dim_idx < dims->size(); ++dim_idx) {
                        ESP_LOGI(TAG, "    Dimension %d: %ld", dim_idx, dims->Get(dim_idx)); // %ld 사용
                    }
                } else {
                    ESP_LOGE(TAG, "    Tensor information not available.");
                }
            }
        }

        // 연산자 정보 출력
        const auto* operators = subgraph->operators();
        for (int op_idx = 0; op_idx < operators->size(); ++op_idx) {
            const auto* op = operators->Get(op_idx);
            auto opcode_index = op->opcode_index();
            auto opcode = model->operator_codes()->Get(opcode_index);
            auto builtin_code = static_cast<tflite::BuiltinOperator>(opcode->builtin_code());
            const char* op_name = tflite::EnumNameBuiltinOperator(builtin_code);

            ESP_LOGI(TAG, "  Operator %lu: %s", static_cast<unsigned long>(op_idx), op_name ? op_name : "CUSTOM");
        }
    }
}

// TensorFlow Lite Micro 초기화
void tflm_init() {
    ESP_LOGI(TAG, "Initializing TensorFlow Lite Micro...");

    // 모델 로드
    const tflite::Model* model = load_model_from_spiffs("/spiffs/wake_word_model.tflite");
    if (!model) {
        ESP_LOGE(TAG, "Failed to load the model from SPIFFS.");
        return;
    }

    // 서브그래프 및 연산자 정보 출력
    print_subgraph_and_operator_info(model);

    // 필요한 연산자만 등록
    static tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddShape();
    resolver.AddStridedSlice();
    resolver.AddPack();
    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
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

    ESP_LOGI(TAG, "Input tensor pointer: %p", (void*)input_tensor);
    ESP_LOGI(TAG, "Input tensor dims pointer: %p", (void*)input_tensor->dims);
    
    // 입력 텐서 정보 확인
    if (input_tensor && input_tensor->dims) {
        ESP_LOGI(TAG, "Input tensor pointer: %p", (void*)input_tensor);
        ESP_LOGI(TAG, "Input tensor dims pointer: %p", (void*)input_tensor->dims);

        // tensor_arena 범위 내인지 확인
        if ((uintptr_t)(input_tensor->dims) < (uintptr_t)tensor_arena ||
            (uintptr_t)(input_tensor->dims) >= (uintptr_t)(tensor_arena + TENSOR_ARENA_SIZE)) {
            ESP_LOGE(TAG, "Input tensor->dims points outside tensor_arena!");
        } else {
            ESP_LOGI(TAG, "Input tensor->dims is within tensor_arena.");
        }

        // dims raw data 출력
        const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(input_tensor->dims);
        for (int i = 0; i < sizeof(TfLiteIntArray); ++i) {
            ESP_LOGI(TAG, "dims raw data[%d]: 0x%02x", i, raw_data[i]);
        }

        // dims->size 및 dims->data 확인
        const TfLiteIntArray* dims = input_tensor->dims;
        ESP_LOGI(TAG, "Input tensor dims size: %d", dims->size);
        for (int i = 0; i < dims->size; ++i) {
            ESP_LOGI(TAG, "Dim %d: %d", i, dims->data[i]);
        }
    } else {
        ESP_LOGE(TAG, "Input tensor or dims pointer is null!");
    }

}

// I2S 데이터 처리 및 모델 실행 (FreeRTOS 태스크)
void process_audio_task(void* arg) {
    ESP_LOGI(TAG, "Starting audio processing task...");
    i2s_chan_handle_t* i2s_rx_channel = (i2s_chan_handle_t*)arg;

    // static으로 선언해서, 함수 호출 간에 데이터가 유지됨.
    static uint8_t buffer[INPUT_SIZE]; // I2S 채널에서 읽어들인 원시 데이터를 저장.
    static int16_t samples[INPUT_SAMPLES]; // I2S 데이터에서 변환된 16비트 PCM 샘플 데이터. 모델 입력 텐서에 넣기 위해 준비.


    while (true) {
        size_t bytes_read = 0;
        
        // I2S RX 채널에서 데이터를 읽어와 buffer에 저장.
        ESP_ERROR_CHECK(i2s_channel_read(*i2s_rx_channel, buffer, INPUT_SIZE, &bytes_read, portMAX_DELAY));

        if (bytes_read != INPUT_SIZE) {
            ESP_LOGW(TAG, "Incomplete audio data read.");
            continue;
        }

        // buffer → samples 변환:
        for (size_t i = 0; i < INPUT_SAMPLES; ++i) {
            samples[i] = static_cast<int16_t>((buffer[2 * i + 1] << 8) | buffer[2 * i]);
            if (i < 10) {  // 처음 10개 샘플 디버깅
                ESP_LOGI(TAG, "Sample %d: %d", i, samples[i]);
            }
        }

        // 입력 텐서 준비
        int8_t* input_data = input_tensor->data.int8; // TensorFlow Lite 모델의 입력 텐서.
        float scale = input_tensor->params.scale; // 입력 데이터의 정규화 스케일 값.
        int zero_point = input_tensor->params.zero_point; // 정수형 데이터에서 0 값의 기준.

        // 샘플 데이터 정규화
        for (size_t i = 0; i < INPUT_SAMPLES; ++i) {
            input_data[i] = static_cast<int8_t>(std::round(samples[i] / 32768.0f / scale) + zero_point);
        }

        // 모델 실행
        if (interpreter->Invoke() != kTfLiteOk) {
            ESP_LOGE(TAG, "Failed to invoke TensorFlow Lite model.");
        }

        // 출력 텐서 처리
        for (int i = 0; i < output_tensor->dims->data[1]; ++i) {
            // 출력 정규화
            float score = (output_tensor->data.int8[i] - output_tensor->params.zero_point) * output_tensor->params.scale;
            ESP_LOGI(TAG, "Output %d: %f", i, score); 
        }
    }
}

extern "C" void app_main(void) {
    i2s_chan_handle_t i2s_rx_channel = nullptr;
    spiffs_init();
    i2s_init(&i2s_rx_channel);
    tflm_init();
    
    // FreeRTOS 태스크 생성 실패 디버깅
    if (xTaskCreatePinnedToCore(
            process_audio_task, "Process Audio Task", 8192, &i2s_rx_channel, 5, nullptr, tskNO_AFFINITY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio processing task! Check memory allocation.");
    }
}
