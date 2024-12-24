#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"

#define I2S_NUM         I2S_NUM_0
#define SAMPLE_RATE     8000
#define I2S_BUFFER_SIZE 8192
#define RECORDING_SIZE  (SAMPLE_RATE * 2)  // 1초 동안 데이터 (16-bit PCM, Mono)
#define UART_BAUD_RATE  921600

static const char *TAG = "INMP441_UART";

void i2s_init(i2s_chan_handle_t *rx_channel) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = I2S_BUFFER_SIZE / 2,
        .auto_clear = true
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, rx_channel));

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
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_STD_WS_WIDTH_AUTO,
            .ws_pol = I2S_STD_WS_POL_NORMAL,
            .bit_shift = I2S_STD_BIT_SHIFT_RIGHT,
            .msb_first = true
        },
        .gpio_cfg = {
            .bclk = 14,  // INMP441의 SCK 핀
            .ws = 26,    // INMP441의 WS 핀
            .dout = I2S_GPIO_UNUSED,
            .din = 32    // INMP441의 SD 핀
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*rx_channel, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*rx_channel));
}

void uart_init() {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        .rx_flow_ctrl_thresh = 122,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 8192, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
}

void record_and_send_audio(i2s_chan_handle_t rx_channel) {
    uint8_t *recording_buffer = malloc(RECORDING_SIZE);
    if (!recording_buffer) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }

    size_t bytes_read = 0;
    size_t total_bytes = 0;

    while (total_bytes < RECORDING_SIZE) {
        size_t bytes_to_read = I2S_BUFFER_SIZE;
        ESP_ERROR_CHECK(i2s_channel_read(rx_channel, recording_buffer + total_bytes, bytes_to_read, &bytes_read, portMAX_DELAY));
        total_bytes += bytes_read;
    }

    ESP_LOGI(TAG, "Recording complete. Sending 1 second data (%d bytes)...", total_bytes);

    // 데이터 시작 태그 전송
    uart_write_bytes(UART_NUM_0, "<DATA_START>", strlen("<DATA_START>"));

    // 오디오 데이터 전송
    uart_write_bytes(UART_NUM_0, (const char *)recording_buffer, total_bytes);

    // 데이터 종료 태그 전송
    uart_write_bytes(UART_NUM_0, "<DATA_END>", strlen("<DATA_END>"));

    ESP_LOGI(TAG, "UART transmission complete.");
    free(recording_buffer);
}


void cleanup(i2s_chan_handle_t rx_channel) {
    ESP_ERROR_CHECK(i2s_channel_disable(rx_channel));
    ESP_ERROR_CHECK(i2s_del_channel(rx_channel));
    ESP_LOGI(TAG, "I2S cleaned up.");
}

void app_main() {
    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_chan_handle_t rx_channel;
    i2s_init(&rx_channel);

    ESP_LOGI(TAG, "Initializing UART...");
    uart_init();

    char rx_buffer[32];
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, rx_buffer, sizeof(rx_buffer) - 1, portMAX_DELAY);
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "Received command: %s", rx_buffer);

            if (strcmp(rx_buffer, "START_RECORDING\n") == 0) {
                ESP_LOGI(TAG, "Command received: START_RECORDING");
                record_and_send_audio(rx_channel);
                cleanup(rx_channel);
                i2s_init(&rx_channel);  // I2S 재초기화
            } else {
                ESP_LOGW(TAG, "Unknown command: %s", rx_buffer);
            }

        }
    }
}
