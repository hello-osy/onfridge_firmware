#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "driver/dac_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DAC_WAV";

// DAC 설정
#define DAC_CHANNEL DAC_CHAN_0 // GPIO 25번 핀 사용
#define SAMPLE_RATE      8000 // WAV 파일 샘플링 속도 (Hz)

// UART 연결 속도
#define UART_BAUD_RATE  115200

// WAV 파일 헤더 크기
#define WAV_HEADER_SIZE 44

// SPIFFS 초기화
void spiffs_init() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5, // 동시에 열 수 있는 최대 파일 수
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition info (%s)", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
}

// WAV 파일 재생 함수
void play_wav(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return;
    }

    // WAV 헤더 건너뛰기
    char wav_header[WAV_HEADER_SIZE];
    if (fread(wav_header, 1, WAV_HEADER_SIZE, file) != WAV_HEADER_SIZE) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        fclose(file);
        return;
    }


    // DAC 설정
    dac_continuous_config_t dac_cfg = {
        .chan_mask = 1 << DAC_CHANNEL,
        .desc_num = 2,
        .buf_size = 512,
        .freq_hz = SAMPLE_RATE,
        .clk_src = DAC_DIGI_CLK_SRC_APLL // APLL 클럭 소스를 사용
    };

    dac_continuous_handle_t dac_handle;
    esp_err_t ret = dac_continuous_new_channels(&dac_cfg, &dac_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DAC (%s)", esp_err_to_name(ret));
        fclose(file);
        return;
    }

    ret = dac_continuous_enable(dac_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable DAC Continuous (%s)", esp_err_to_name(ret));
        dac_continuous_del_channels(dac_handle);
        fclose(file);
        return;
    }

    uint8_t buffer[256];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        size_t bytes_loaded = 0;
        ret = dac_continuous_write(dac_handle, buffer, bytes_read, &bytes_loaded, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC (%s)", esp_err_to_name(ret));
            break;
        }
    }

    // DAC Continuous 비활성화
    dac_continuous_disable(dac_handle);
    dac_continuous_del_channels(dac_handle);
    fclose(file);

    ESP_LOGI(TAG, "Finished playing WAV file: %s", file_path);
}


void app_main(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    spiffs_init();

    while (1) {
        ESP_LOGI(TAG, "Playing WAV file...");
        play_wav("/spiffs/test.wav");
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1초 대기 후 반복
    }
}
