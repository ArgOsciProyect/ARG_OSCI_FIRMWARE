#include <stdio.h>
#include <stdint.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"

// Definición de pines para HSPI (modo esclavo)
#define GPIO_HANDSHAKE 2
#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15

// Definición de pines para VSPI (modo maestro)
#define PIN_NUM_MISO_TX 19
#define PIN_NUM_MOSI_TX 23
#define PIN_NUM_CLK_TX  18
#define PIN_NUM_CS_TX   5

// Tamaño del buffer de recepción y transmisión
#define BUFFER_SIZE 1440

#define SEND_MULTIPLIER 6  // Multiplicador de envío: número de recepciones a acumular


// Parámetros de la señal
#define SAMPLE_RATE 2000000  // Frecuencia de muestreo 2 MHz
#define SIGNAL_FREQ 100000   // Frecuencia de la señal 100 kHz
#define PI 3.14159265359

#define WIFI_SSID_STA "IPLAN-1_D"
#define WIFI_PASS_STA "santiagojoaquin"
#define WIFI_SSID_AP "ESP32"
#define WIFI_PASS_AP "12345678"
#define MAX_STA_CONN 1
#define PORT 8080

static const char *TAG = "wifi_example";
const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t s_wifi_event_group;

// Buffers

// Definir el canal DMA
#define DMA_CHANNEL_RX 1
#define DMA_CHANNEL_TX 2

spi_device_handle_t spi;  // Manejador del dispositivo SPI
int sock;  // Socket para la transmisión de datos

// Contadores de datos
volatile int datos_enviados = 0;
volatile int datos_recibidos = 0;

// Configuración del SPI en modo esclavo
void my_spi_slave_initialize() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = PIN_NUM_CS,
        .flags = 0,
        .queue_size = 10,
        .mode = 0,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL
    };

    gpio_set_direction(GPIO_HANDSHAKE, GPIO_MODE_OUTPUT);

    // Inicializar el bus SPI en modo esclavo
    ESP_ERROR_CHECK(spi_slave_initialize(HSPI_HOST, &buscfg, &slvcfg, DMA_CHANNEL_RX));
}

// Configuración del SPI en modo maestro para transmisión
void my_spi_master_initialize() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI_TX,
        .miso_io_num = PIN_NUM_MISO_TX,
        .sclk_io_num = PIN_NUM_CLK_TX,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 80 * 1000 * 1000,  // 30 MHz
        .mode = 0,
        .spics_io_num = PIN_NUM_CS_TX,
        .queue_size = 10,
    };

    // Inicializar el bus SPI en modo maestro
    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, DMA_CHANNEL_TX));
    // Añadir el dispositivo SPI al bus
    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &devcfg, &spi));
}

// Generar la señal senoidal de 100 kHz
void generate_sine_wave() {

}

// Configurar la ESP32 como Access Point
void wifi_init_softap(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID_AP,
            .ssid_len = strlen(WIFI_SSID_AP),
            .password = WIFI_PASS_AP,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .beacon_interval = 100,
            .channel = 1,
            .ssid_hidden = 0,
            .pairwise_cipher = WIFI_CIPHER_TYPE_NONE
        },
    };

    if (strlen(WIFI_PASS_AP) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    // Set the required Wi-Fi configurations
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_AP, WIFI_BW_HT40));  // Set 40 MHz bandwidth
    //ESP_ERROR_CHECK(esp_wifi_config_80211_tx_rate(ESP_IF_WIFI_AP, WIFI_PHY_RATE_54M));  // Set 6 Mbps data rate
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(30));  // Set max transmit power to 19 dBm

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", WIFI_SSID_AP, WIFI_PASS_AP);
}

// Crear socket para enviar los datos
void socket_create() {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(3333);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    listen(sock, 1);

    printf("Esperando conexión de un cliente...\n");
    struct sockaddr_in source_addr;
    uint addr_len = sizeof(source_addr);
    sock = accept(sock, (struct sockaddr *)&source_addr, &addr_len);

    printf("Cliente conectado\n");
}

// Tarea para manejar la recepción de datos y enviarlos al socket
void spi_slave_task(void *arg) {
    // Asegurarse de que el buffer esté alineado a 4 bytes
    uint16_t rx_buffer[BUFFER_SIZE] __attribute__((aligned(4)));
    uint16_t accumulated_buffer[SEND_MULTIPLIER * BUFFER_SIZE];  // Buffer para acumular recepciones
    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = BUFFER_SIZE * 16;  // 16 bits por dato
    t.rx_buffer = rx_buffer;

    int reception_count = 0;

    while (1) {
        // Esperar a que llegue una transacción
        ESP_ERROR_CHECK(spi_slave_transmit(HSPI_HOST, &t, portMAX_DELAY));

        // Copiar los datos recibidos al buffer acumulado
        memcpy(&accumulated_buffer[reception_count * BUFFER_SIZE], rx_buffer, BUFFER_SIZE * sizeof(uint16_t));
        reception_count++;

        // Si se han acumulado SEND_MULTIPLIER recepciones, enviar los datos por el socket
        if (reception_count == SEND_MULTIPLIER) {
            send(sock, accumulated_buffer, SEND_MULTIPLIER * BUFFER_SIZE * sizeof(uint16_t), 0);
            reception_count = 0;  // Reiniciar el contador de recepciones
        }

        // Imprimir el contador de datos recibidos
        //printf("Datos recibidos: %d\n", datos_recibidos);
    }
}

// Tarea para transmitir los datos a través del SPI maestro
void spi_master_task(void *arg) {
    uint16_t tx_buffer[BUFFER_SIZE] __attribute__((aligned(4)));
    for (int i = 0; i < BUFFER_SIZE; i++) {
        float t = (float)i / SAMPLE_RATE;  // Tiempo en segundos para cada muestra
        tx_buffer[i] = (uint16_t)((sin(2 * PI * SIGNAL_FREQ * t) + 1) * (2047));  // Señal normalizada a 12 bits
    }

    spi_device_handle_t spi = (spi_device_handle_t)arg;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = BUFFER_SIZE * 16;  // 16 bits por dato
    t.tx_buffer = tx_buffer;

    while (1) {
        // Transmitir los datos
        ESP_ERROR_CHECK(spi_device_transmit(spi, &t));  // Transmitir los datos
        //datos_enviados += BUFFER_SIZE;

        // Imprimir el contador de datos enviados
        //printf("Datos enviados: %d\n", datos_enviados);

        //esp_rom_delay_us(BUFFER_SIZE/2);
    }
}

void app_main() {
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Configurar el AP Wi-Fi
    wifi_init_softap();

    // Crear el socket
    socket_create();

    // Inicializar el SPI en modo esclavo
    my_spi_slave_initialize();

    // Inicializar el SPI en modo maestro
    my_spi_master_initialize();

    // Generar la señal senoidal
    generate_sine_wave();

    // Crear la tarea de recepción de datos en core 0
    xTaskCreatePinnedToCore(spi_slave_task, "spi_slave_task", BUFFER_SIZE*SEND_MULTIPLIER*2.5, NULL, 5, NULL, 0);

    // Crear la tarea de transmisión de datos en core 1 y pasar el manejador del dispositivo SPI
    xTaskCreatePinnedToCore(spi_master_task, "spi_master_task", BUFFER_SIZE*SEND_MULTIPLIER*2.5, (void*)spi, 5, NULL, 1);
}