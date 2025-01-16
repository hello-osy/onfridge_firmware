#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MEMORY_STATUS";

void check_memory_types() {
    size_t dram_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t iram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);

    ESP_LOGI(TAG, "Free DRAM (8-bit accessible): %u bytes", dram_free);
    ESP_LOGI(TAG, "Free IRAM (32-bit accessible): %u bytes", iram_free);
}

void dump_heap_info() {
    ESP_LOGI(TAG, "Heap Info:");
    heap_caps_dump(MALLOC_CAP_DEFAULT);
}

void check_task_stack() {
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Current task stack high watermark: %u bytes", watermark);
}

void memory_status_task(void *arg) {
    ESP_LOGI(TAG, "Memory Status:");

    // 전체 힙 상태 출력
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Total free heap size: %u bytes", free_heap);

    // DRAM과 IRAM 상태 출력
    check_memory_types();

    // 힙 정보 디버깅
    dump_heap_info();

    // FreeRTOS 태스크 스택 상태 확인
    check_task_stack();

    vTaskDelete(NULL); // 태스크 종료
}

extern "C" void app_main(void) {
    // FreeRTOS 태스크 생성
    if (xTaskCreate(memory_status_task, "MemoryStatusTask", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Memory Status Task!");
    }
}
