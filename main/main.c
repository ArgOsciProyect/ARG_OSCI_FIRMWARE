#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_adc/adc_continuous.h"
#include "esp_netif.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include <math.h>

#define ADC_CHANNEL ADC_CHANNEL_5
#define SAMPLE_RATE 2000000 // 2 MHz
#define I2S_NUM         (0)
#define BUF_SIZE 8192

static const char *TAG = "ARG_OSCI";
int read_miss_count = 0;
uint32_t samples_p_second = 0;

void i2s_adc_init()
{
    // Configurar el ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_12);

    // Configurar el I2S en modo ADC
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = SAMPLE_RATE*6,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        .dma_buf_len = 1024,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 70000000
    };

    // Instalar y configurar el driver I2S
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);

    // Configurar el ADC para el I2S
    i2s_set_adc_mode(ADC_UNIT_1, ADC_CHANNEL);
}

void adc_test(void *pvParameters) {
    uint8_t buffer[BUF_SIZE];
    uint32_t len;
    uint32_t sum;
    const int duration_ms = 10000;  // Duración total de ejecución en milisegundos (10 segundos)
    TickType_t start_time;

    while (1) {
        sum = 0;
        start_time = xTaskGetTickCount(); // Marca el tiempo de inicio
        i2s_adc_enable(I2S_NUM);
        while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(duration_ms)){
            int ret = i2s_read(I2S_NUM, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            if (ret == ESP_OK && len > 0) {
                sum += len;
            } else {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10) {  
                    ESP_LOGE(TAG, "Critical ADC data loss detected.");
                    read_miss_count = 0;
                }
            }
        }
        i2s_adc_disable(I2S_NUM);
        samples_p_second = sum / 20;
        ESP_LOGI(TAG, "Samples per second: %lu", samples_p_second);
    }
}

void app_main() {
    ESP_LOGI(TAG, "Starting ARG_OSCI");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    i2s_adc_init();

    xTaskCreate(adc_test, "adc_test", BUF_SIZE * 1.3, NULL, 5, NULL);
}
