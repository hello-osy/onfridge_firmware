#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"

#define I2S_NUM         I2S_NUM_0
#define SAMPLE_RATE     16000
#define DMA_BUFFER_COUNT 2
#define I2S_BUFFER_SIZE 8000  // I2S 데이터 처리를 위해 필요한 전체 DMA 버퍼 크기
#define RECORDING_SECONDS 5  // 녹음 시간 (초)
#define RECORDING_SIZE  (SAMPLE_RATE * 2 * RECORDING_SECONDS)   // RECORDING_SECONDS초 데이터 크기
#define UART_BAUD_RATE  115200
#define UART_CHUNK_SIZE 256                       // UART 전송 청크 크기

static const char *TAG = "INMP441_UART";

// I2S가 오디오 데이터를 읽고, DMA가 메모리로 전송하며, UART가 데이터를 외부로 전달.

// INMP441 마이크: 아날로그 신호 → ADC → I2S 디지털 신호
// ESP32(src/microphone.c): → I2S 하드웨어(ESP32) → DMA 버퍼 → UART
// 컴퓨터(sound_receiver.py): → raw파일 → wav파일


// **I2S 데이터 흐름**:
// 1. I2S 신호는 ESP32 내부의 I2S FIFO 버퍼(512바이트)에 저장됩니다.
// 2. I2S 하드웨어가 FIFO 데이터를 DMA 버퍼로 전송합니다.
// 3. DMA 버퍼는 SRAM에 위치하며, 필요한 만큼의 데이터를 수집합니다.
// 4. CPU가 DMA 버퍼에서 데이터를 읽어 UART로 전송합니다.

// **DMA와 FIFO 관계**:
// - FIFO 크기는 ESP32에서 약 512바이트입니다.
// - DMA 버퍼 크기는 설정에 따라 다르며, 예를 들어 1KB로 설정하면 FIFO 데이터를 두 번 채워야 DMA 버퍼가 꽉 찹니다.
// - FIFO 크기보다 DMA 버퍼 크기가 크면 CPU가 데이터를 읽는 빈도를 줄일 수 있어 효율적입니다.

// **UART 데이터 흐름**:
// 1. CPU는 DMA 버퍼에서 데이터를 읽고, UART 하드웨어로 전송합니다.
// 2. UART 하드웨어는 송신 FIFO를 통해 데이터를 송출하며, 외부 장치(예: PC)에서 수신됩니다.

// **메모리 할당**:
// - `record_and_send_audio` 함수는 녹음 데이터를 저장하기 위해 SRAM에서 DMA 버퍼 크기에 따라 동적으로 메모리를 할당합니다.
// - ESP32의 SRAM은 약 520KB이며, 시스템과 애플리케이션에 의해 공유되므로 적절한 메모리 관리가 필요합니다.

// **UART 수신 버퍼**:
// - `uart_driver_install()` 함수의 수신 버퍼는 SRAM에 위치합니다.
// - 이 버퍼는 UART로 수신된 데이터를 임시로 저장하며, 애플리케이션에서 읽을 때까지 유지됩니다.
// - 수신 버퍼 크기는 시스템 메모리 상태와 데이터 처리 요구 사항에 맞게 조정해야 합니다.

void i2s_init(i2s_chan_handle_t *i2s_rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = DMA_BUFFER_COUNT,
        .dma_frame_num = I2S_BUFFER_SIZE / DMA_BUFFER_COUNT,
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
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, // 데이터 비트: 16비트
            .slot_bit_width = I2S_DATA_BIT_WIDTH_16BIT, // 슬롯 비트: 16비트
            .slot_mode = I2S_SLOT_MODE_MONO,            // 모노 모드
            .slot_mask = I2S_STD_SLOT_LEFT              // 좌측 슬롯 사용
        },
        .gpio_cfg = {
            .bclk = 14,  // INMP441의 SCK 핀
            .ws = 15,    // INMP441의 WS 핀
            .dout = I2S_GPIO_UNUSED,
            .din = 32    // INMP441의 SD 핀
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*i2s_rx_channel, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*i2s_rx_channel));
    ESP_LOGI(TAG, "I2S initialized successfully.");
}

void uart_init() {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 33000, 0, 0, NULL, 0)); //32000+여유 공간
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_LOGI(TAG, "UART initialized successfully.");
}

void record_and_send_audio(i2s_chan_handle_t i2s_rx_channel) {
    uint8_t *buffer_a = malloc(I2S_BUFFER_SIZE);
    uint8_t *buffer_b = malloc(I2S_BUFFER_SIZE);

    if (!buffer_a || !buffer_b) {
        ESP_LOGE(TAG, "Memory allocation failed.");
        if (buffer_a) free(buffer_a);
        if (buffer_b) free(buffer_b);
        return;
    }

    uint8_t *current_buffer = buffer_a;
    uint8_t *send_buffer = buffer_b;

    size_t bytes_read = 0;

    ESP_LOGI(TAG, "Starting %d seconds recording.", RECORDING_SECONDS);

    for (int second = 0; second < RECORDING_SECONDS; second++) {
        // START 전송
        uart_write_bytes(UART_NUM_0, "<DATA_START>", strlen("<DATA_START>"));
        
        // 각 버퍼를 처리하는 반복문(1초 분량 채울 때까지 버퍼 스왑 계속 ㄱㄱ)
        for (int buffer_index = 0; buffer_index < 4; buffer_index++) {
            // I2S에서 데이터 읽기
            ESP_ERROR_CHECK(i2s_channel_read(i2s_rx_channel, current_buffer, I2S_BUFFER_SIZE, &bytes_read, portMAX_DELAY));
            if (bytes_read < I2S_BUFFER_SIZE) {
                ESP_LOGW(TAG, "Incomplete I2S read for buffer %d: Expected %d bytes, got %d bytes.", 
                        buffer_index + 1, I2S_BUFFER_SIZE, bytes_read);
            }
            // UART로 데이터 전송
            uart_write_bytes(UART_NUM_0, (const char *)current_buffer, bytes_read);

            // 버퍼 스왑
            uint8_t *temp = current_buffer;
            current_buffer = send_buffer;
            send_buffer = temp;
        }


        // END 전송
        uart_write_bytes(UART_NUM_0, "<DATA_END>", strlen("<DATA_END>"));

        ESP_LOGI(TAG, "Finished sending data for second %d.", second + 1);
    }

    ESP_LOGI(TAG, "Recording and transmission completed.");
    free(buffer_a);
    free(buffer_b);
}


void app_main() {
    i2s_chan_handle_t i2s_rx_channel;
    i2s_init(&i2s_rx_channel);
    uart_init();

    esp_log_level_set("*", ESP_LOG_NONE); //모든 로그가 출력되지 않도록 함.(esp초기화 로그 때문에 녹음 시작 명령어가 묻힘.)
    uart_flush(UART_NUM_0);               // UART 버퍼 비우기

    char uart_command[32];
    while (1) {
        memset(uart_command, 0, sizeof(uart_command)); //uart_command 배열의 모든 바이트를 0
        int len = uart_read_bytes(UART_NUM_0, uart_command, sizeof(uart_command) - 1, portMAX_DELAY);
        if (len <= 0) {
            ESP_LOGW(TAG, "No command received.");
            continue;  // 명령이 없으면 루프 재시작
        }
        if (len > 0) {
            uart_command[len] = '\0';
            if (strncmp(uart_command, "START_RECORDING", strlen("START_RECORDING")) == 0) {
                ESP_LOGI(TAG, "Command received: START_RECORDING");
                record_and_send_audio(i2s_rx_channel);
            } else {
                ESP_LOGW(TAG, "Unknown command: %s", uart_command);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기
    }
    
    ESP_LOGI(TAG, "Application terminated.");
}