#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PWM_WAV";

// PWM 채널 및 타이머 설정
#define PWM_CHANNEL      LEDC_CHANNEL_0
#define PWM_TIMER        LEDC_TIMER_0
#define PWM_GPIO_PIN     26 // 스피커의 IN 핀 연결
#define SAMPLE_RATE      16000 // WAV 파일 샘플링 속도 (Hz)
#define UART_BAUD_RATE  115200

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

// PWM 설정
void pwm_init() {
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_10_BIT, // 10비트 해상도 (0-1023)
        .freq_hz = SAMPLE_RATE,              // WAV 파일의 샘플링 속도와 일치
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = PWM_TIMER
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = PWM_CHANNEL,
        .duty       = 0,
        .gpio_num   = PWM_GPIO_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = PWM_TIMER
    };
    ledc_channel_config(&ledc_channel);
}

// WAV 파일 읽기 및 PWM 출력
void play_wav(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return;
    }

    char wav_header[44]; // WAV 파일 헤더는 44바이트
    fread(wav_header, 1, 44, file); // 헤더 읽기

    uint8_t buffer[256];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            // PWM 듀티 사이클을 WAV 데이터에 매핑
            uint32_t duty = buffer[i] * 4; // 8비트 WAV 데이터를 10비트 듀티로 변환
            ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);

            // 샘플 속도에 맞게 대기
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }

    fclose(file);
    ESP_LOGI(TAG, "Finished playing WAV file: %s", file_path);
}

void app_main() {
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    spiffs_init();

    ESP_LOGI(TAG, "Initializing PWM...");
    pwm_init();

    // 무한 루프를 통해 WAV 파일 재생 반복
    while (1) {
        ESP_LOGI(TAG, "Playing WAV file...");
        play_wav("/spiffs/test.wav");
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 1초 대기
    }
}
