#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <SPI.h>
#include <string.h>
#include <math.h>

#define PI 3.14159265359
#define SAMPLE_RATE 2000000  // Frecuencia de muestreo 2 MHz
#define SIGNAL_FREQ 100000   // Frecuencia de la señal 100 kHz
#define BUFFER_SIZE 2048       // Tamaño del buffer de transmisión (20 muestras)
#define DELAY BUFFER_SIZE/2  // Retardo entre transmisiones
#define DELAY_CYCLES 40      // Número de ciclos para un retardo de 0.5 us (ajustar según sea necesario)

#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15

static const char *TAG = "spi_slave_dma";

// Buffer de la señal senoidal
DMA_ATTR uint16_t sine_wave_buffer[BUFFER_SIZE];

// Función para generar la señal senoidal y almacenarla en un buffer
void generate_sine_wave(uint16_t *buffer, int length) {
    for (int i = 0; i < length; i++) {
        float t = (float)i / length;
        buffer[i] = (uint16_t)((sin(2 * PI * t) + 1) * 2047);  // Señal normalizada a 12 bits
    }
    ESP_LOGI(TAG, "Senoidal generada y almacenada en el buffer.");
    //for (int i = 0; i < length; i++) {
    //    ESP_LOGI(TAG, "Muestra %d: %d", i, buffer[i]);
    //}
}

// Tarea para simular la respuesta del ADC por SPI
void spi_slave_task(void *arg) {
    spi_slave_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.rx_buffer = NULL;  // No se espera respuesta del maestro
    while (1) {
        trans.length = BUFFER_SIZE * 16;  // 16 bits por muestra, 20 muestras
        trans.tx_buffer = sine_wave_buffer;  // Enviar el buffer completo
        // Esperar a que el maestro inicie la transacción SPI
        esp_err_t ret = spi_slave_transmit(HSPI_HOST, &trans, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error en la transmisión SPI: %d", ret);
        }
    }
}

void my_spi_slave_initialize() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_SIZE * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_SLAVE | SPICOMMON_BUSFLAG_IOMUX_PINS
    };

    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = PIN_NUM_CS,
        .flags = 0,
        .queue_size = 1,
        .mode = 0,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL
    };

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(spi_slave_initialize(HSPI_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));
}

void app_main() {
    generate_sine_wave(sine_wave_buffer, BUFFER_SIZE);
    my_spi_slave_initialize();
    xTaskCreatePinnedToCore(spi_slave_task, "spi_slave_task", 2048, NULL, 5, NULL, 1);
}