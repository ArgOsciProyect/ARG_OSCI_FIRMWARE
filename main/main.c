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
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <math.h>
#include <driver/ledc.h>

#define ADC_CHANNEL ADC_CHANNEL_5
#define SAMPLE_RATE 2000000 // 2 MHz
#define BUF_SIZE 8192
#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15

static const char *TAG = "ARG_OSCI";
int read_miss_count = 0;
uint32_t samples_p_second = 0;

// Add to global variables
static ledc_channel_config_t ledc_channel;
static spi_device_handle_t spi;
#define TRIGGER_PWM_FREQ 3000000 // 25kHz
#define TRIGGER_PWM_TIMER LEDC_TIMER_0
#define TRIGGER_PWM_CHANNEL LEDC_CHANNEL_0
#define TRIGGER_PWM_GPIO GPIO_NUM_26      // Choose appropriate GPIO
#define TRIGGER_PWM_RES LEDC_TIMER_4_BIT // 4-bit resolution (0-16)
#define TRIGGER_PWM_DUTY 31.25          // 31.25% duty cycle

void init_trigger_pwm(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = TRIGGER_PWM_RES,
        .freq_hz = TRIGGER_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = TRIGGER_PWM_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel.channel = TRIGGER_PWM_CHANNEL;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = TRIGGER_PWM_GPIO;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = TRIGGER_PWM_TIMER;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, TRIGGER_PWM_DUTY);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}


void spi_master_init()
{
    esp_err_t ret;

    // Configurar el bus SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUF_SIZE
    };

    // Inicializar el bus SPI
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 3);
    ESP_ERROR_CHECK(ret);

    // Configurar el dispositivo SPI
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 48 * 1000 * 1000, // Velocidad del reloj SPI (10 MHz)
        .mode = 0,                          // Modo SPI 0
        .spics_io_num = PIN_NUM_CS,         // Pin CS
        .queue_size = BUF_SIZE,                    // Tamaño de la cola de transacciones
        .pre_cb = NULL,                     // Callback antes de cada transacción
        .post_cb = NULL                     // Callback después de cada transacción
    };

    // Inicializar el dispositivo SPI

    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master initialized.");
}

void spi_test(void *pvParameters) {
    uint8_t buffer[BUF_SIZE];
    spi_transaction_t t;
    memset(&t, 0, sizeof(t)); // Clear the transaction structure
    t.length = BUF_SIZE * 8;  // Length in bits
    t.rx_buffer = buffer;     // Pointer to the buffer to receive data
    uint32_t len;
    uint32_t sum;
    const int duration_ms = 10000;  // Duración total de ejecución en milisegundos (10 segundos)
    TickType_t start_time;

    while (1) {
        sum = 0;
        start_time = xTaskGetTickCount(); // Marca el tiempo de inicio
        // Poner en cola la transacción
        esp_err_t ret = spi_device_queue_trans(spi, &t, 1000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(ret);

        while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(duration_ms)){
             
             // Obtener el resultado de la transacción
             spi_transaction_t *rtrans;
             ret = spi_device_get_trans_result(spi, &rtrans, 1000 / portTICK_PERIOD_MS);
             ESP_ERROR_CHECK(ret);

             // Poner en cola la transacción
             esp_err_t ret = spi_device_queue_trans(spi, &t, 1000 / portTICK_PERIOD_MS);
             ESP_ERROR_CHECK(ret);
 
             if (ret == ESP_OK) {
                 len = rtrans->rxlength / 8; // rxlength is in bits
                 sum += len;
             } else {
                 read_miss_count++;
                 ESP_LOGW(TAG, "Missed SPI readings! Count: %d", read_miss_count);
                 if (read_miss_count >= 10) {
                     ESP_LOGE(TAG, "Critical SPI data loss detected.");
                     read_miss_count = 0;
                 }
             }
        }
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
    spi_master_init();
    init_trigger_pwm();

    xTaskCreate(spi_test, "spi_test", BUF_SIZE * 2.6, NULL, 5, NULL);
}
