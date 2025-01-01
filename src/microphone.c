#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"

#define I2S_NUM         I2S_NUM_0
#define SAMPLE_RATE     8000
#define DMA_BUFFER_COUNT 6
#define I2S_BUFFER_SIZE (DMA_BUFFER_COUNT * 1024)  // I2S 데이터 처리를 위해 필요한 전체 DMA 버퍼 크기(1kb 짜리 DMA 버퍼 6개)
#define RECORDING_SIZE  (SAMPLE_RATE * 2)         // 1초 데이터 크기
#define UART_BAUD_RATE  115200
#define UART_CHUNK_SIZE 512                       // UART 전송 청크 크기

static const char *TAG = "INMP441_UART";

// I2S가 오디오 데이터를 읽고, DMA가 메모리로 전송하며, UART가 데이터를 외부로 전달.

// INMP441 마이크: 아날로그 신호 → ADC → I2S 디지털 신호
// ESP32(src/microphone.c): → I2S 하드웨어(ESP32) → DMA 버퍼 → UART
// 컴퓨터(sound_receiver.py): → raw파일 → wav파일

// I2S 신호는 ESP32 내부의 I2S FIFO 버퍼에 저장됩니다.
// I2S 하드웨어의 FIFO에 저장된 데이터를 DMA가 메모리(DMA 버퍼)로 전송합니다.
// CPU는 DMA 버퍼에서 데이터를 읽어옵니다.
// UART는 CPU가 가져온 데이터를 외부 장치(예: PC)로 송신합니다.

// I2S FIFO 버퍼에 계속 데이터가 들어오고, FIFO가 DMA 버퍼에 데이터를 필요한 만큼 나눠서 채워주는 구조입니다.
// I2S FIFO 버퍼는 ESP32에서는 약 512바이트입니다.
// DMA 버퍼가 1KB라면 FIFO는 512바이트를 두 번 전송하여 DMA 버퍼를 채웁니다.
// DMA 버퍼가 512바이트라면 CPU는 512바이트마다 데이터를 읽어야 하지만, 1KB라면 1024바이트마다 읽으면 됩니다.

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
            .slot_mask = I2S_STD_SLOT_RIGHT             // 우측 슬롯 사용
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
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 16384, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_LOGI(TAG, "UART initialized successfully.");
}

void record_and_send_audio(i2s_chan_handle_t i2s_rx_channel) {
    uint8_t *audio_buffer = malloc(RECORDING_SIZE);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Memory allocation failed.");
        return;  // 재부팅 대신 함수 종료
    }

    size_t total_bytes = 0, bytes_read = 0;

    // I2S로 오디오 데이터 읽기
    while (total_bytes < RECORDING_SIZE) {
        ESP_ERROR_CHECK(i2s_channel_read(i2s_rx_channel, audio_buffer + total_bytes, I2S_BUFFER_SIZE, &bytes_read, portMAX_DELAY));
        total_bytes += bytes_read;
    }

    // UART로 데이터 전송
    ESP_LOGI(TAG, "Sending %d bytes via UART", total_bytes);
    uart_write_bytes(UART_NUM_0, "<DATA_START>", strlen("<DATA_START>"));
    for (size_t i = 0; i < total_bytes; i += UART_CHUNK_SIZE) {
        size_t chunk_size = (total_bytes - i > UART_CHUNK_SIZE) ? UART_CHUNK_SIZE : total_bytes - i;
        uart_write_bytes(UART_NUM_0, (const char *)(audio_buffer + i), chunk_size);
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms 지연
    }
    uart_write_bytes(UART_NUM_0, "<DATA_END>", strlen("<DATA_END>"));

    ESP_LOGI(TAG, "Data sent successfully.");
    free(audio_buffer);
}

void app_main() {
    i2s_chan_handle_t i2s_rx_channel;
    i2s_init(&i2s_rx_channel);
    uart_init();

    char uart_command[32];
    while (1) {
        memset(uart_command, 0, sizeof(uart_command));
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
    // 종료 전 자원 해제
    ESP_LOGI(TAG, "Releasing resources...");
    i2s_channel_disable(i2s_rx_channel);
    i2s_del_channel(i2s_rx_channel);
    uart_driver_delete(UART_NUM_0);

    ESP_LOGI(TAG, "Application terminated.");
}