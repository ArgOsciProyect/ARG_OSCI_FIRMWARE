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
//#include "driver/mcpwm.h"
#include "driver/mcpwm_prelude.h"
#include "esp_task_wdt.h"
#define ADC_CHANNEL ADC_CHANNEL_5
#define SAMPLE_RATE 2000000 // 2 MHz
#define BUF_SIZE 2*8192
#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15

static const char *TAG = "ARG_OSCI";
int read_miss_count = 0;
uint32_t samples_p_second = 0;

// Add to global variables
static spi_device_handle_t spi;
#define TRIGGER_PWM_FREQ_HZ 2500000  // 2.5MHz
#define TRIGGER_PWM_GPIO 16
#define TRIGGER_DUTY_CYCLE 31.25    // 31.25% duty cycle
#define SYNC_GPIO 17  // Usar un pin diferente para la sincronización
static mcpwm_timer_handle_t timer = NULL;
static mcpwm_oper_handle_t oper = NULL;
static mcpwm_cmpr_handle_t comparator = NULL;
static mcpwm_gen_handle_t generator = NULL;



void init_trigger_pwm(void)
{
    // Configurar el pin CS como entrada para la sincronización
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SYNC_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Create timer and operator");
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TRIGGER_PWM_FREQ_HZ * 32,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 32,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

   // Crear una fuente de sync GPIO
   mcpwm_gpio_sync_src_config_t gpio_sync_config = {
    .group_id = 0,
    .gpio_num = SYNC_GPIO,
    .flags = {
        .active_neg = 1,        // Activo en flanco de bajada
        .io_loop_back = 0,      // No necesitamos loop back
        .pull_down = 1,         // Pull-down habilitado
        .pull_up = 0,           // Pull-up deshabilitado
    }
};
mcpwm_sync_handle_t gpio_sync = NULL;
ESP_ERROR_CHECK(mcpwm_new_gpio_sync_src(&gpio_sync_config, &gpio_sync));

// Configurar la fase en el evento de sync con la fuente GPIO
mcpwm_timer_sync_phase_config_t sync_phase = {
    .sync_src = gpio_sync,    // Usar la fuente de sync GPIO
    .count_value = 0,         // Reiniciar el contador a 0
    .direction = MCPWM_TIMER_DIRECTION_UP
};
ESP_ERROR_CHECK(mcpwm_timer_set_phase_on_sync(timer, &sync_phase));

    // ... resto de la configuración del operador y comparador ...
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

    ESP_LOGI(TAG, "Connect timer and operator");
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    ESP_LOGI(TAG, "Create comparator and generator from the operator");
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = TRIGGER_PWM_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

     // Configurar las acciones del generador
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));  // Cambiado a LOW
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_HIGH))); // Cambiado a HIGH

    // Configurar el duty cycle - Ajustado para 22 ticks en bajo
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, (uint32_t)(26)));

    ESP_LOGI(TAG, "Enable PWM generator output");
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(generator, -1, true));

    ESP_LOGI(TAG, "Start timer");
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}


void spi_master_init()
{
    esp_err_t ret;
    int freq;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_MISO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configurar el bus SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2*BUF_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER | 
                SPICOMMON_BUSFLAG_MISO |
                SPICOMMON_BUSFLAG_IOMUX_PINS,
        .intr_flags = ESP_INTR_FLAG_IRAM
    };

    // Inicializar el bus SPI
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 3);
    ESP_ERROR_CHECK(ret);

    // Configurar el dispositivo SPI
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 48 * 1000 * 1000, // Velocidad del reloj SPI (10 MHz)
        .mode = 0,                          // Modo SPI 0
        .spics_io_num = PIN_NUM_CS,         // Pin CS
        .queue_size = 7,                    // Tamaño de la cola de transacciones
        .pre_cb = NULL,                     // Callback antes de cada transacción
        .post_cb = NULL,                     // Callback después de cada transacción
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
        .cs_ena_pretrans = 11,
        .input_delay_ns = 1
    };

    // Inicializar el dispositivo SPI

    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master initialized.");
    spi_device_get_actual_freq(spi, &freq);
    ESP_LOGI(TAG, "Actual frequency: %d", freq);

}

void spi_test(void *pvParameters) {
    uint16_t buffer1[BUF_SIZE];
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;               
    t.rxlength = BUF_SIZE * 16; 
    t.rx_buffer = buffer1;      
    t.flags = 0;               

    for(int iter = 0; iter < 100; iter++) {
        // Realizar una única transacción
        esp_err_t ret = spi_device_polling_transmit(spi, &t);

        if (ret == ESP_OK) {
            if(iter == 99) {  // Solo mostrar la última iteración
                ESP_LOGI(TAG, "Datos recibidos en la iteración 100:");
                for (int i = 0; i < BUF_SIZE; i = i+200) {
                    printf("%5d\n", buffer1[i]);
                }
            }
        } else {
            ESP_LOGE(TAG, "Error en la lectura SPI en iteración %d", iter + 1);
            break;
        }
    }

    // La tarea ha terminado
    vTaskDelete(NULL);
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

    ESP_ERROR_CHECK(esp_task_wdt_deinit());

    spi_master_init();
    init_trigger_pwm();

    xTaskCreatePinnedToCore(spi_test, "spi_test", BUF_SIZE *3, NULL, 5, NULL, 0);
}

