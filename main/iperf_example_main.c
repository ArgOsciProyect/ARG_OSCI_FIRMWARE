#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/spi_master.h>
#include <lwip/sockets.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_log.h>

#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15
#define BUFFER_SIZE 2048 // Tamaño del buffer de SPI DMA (en palabras de 16 bits)
#define PACKET_SIZE 1440 // Número de palabras de 16 bits por paquete TCP
#define SPI_FREQ_HZ SPI_MASTER_FREQ_26M
#define WIFI_SSID_AP "ESP32"
#define WIFI_PASS_AP "12345678"
#define MAX_STA_CONN 1
#define PORT 8080

static spi_device_handle_t spi;
static int sock;
static SemaphoreHandle_t spiDataReady;

// Buffer compartido para SPI DMA y TCP
DMA_ATTR static uint16_t recv_buffer[BUFFER_SIZE];

static const char *TAG = "wifi_spi_example";
volatile bool is_connected = false;

// Inicialización del Wi-Fi como Access Point
static void wifi_init_softap(void) {
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
        },
    };

    if (strlen(WIFI_PASS_AP) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Configuración del SPI en modo maestro con DMA
static void my_spi_master_initialize() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .max_transfer_sz = BUFFER_SIZE * sizeof(uint16_t),
        .isr_cpu_id = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_FREQ_HZ,  
        .mode = 0,                       
        .spics_io_num = PIN_NUM_CS,      
        .queue_size = 10,                
        .duty_cycle_pos = 128,
        .input_delay_ns = 50,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &devcfg, &spi));
}

// Tarea para recibir datos por SPI usando DMA
void spi_receive_task(void *arg) {
    spi_transaction_t trans = {
        .length = BUFFER_SIZE * 16, // Tamaño en bits
        .tx_buffer = NULL,
        .rx_buffer = recv_buffer,
    };
    while (1) {
        trans.length = BUFFER_SIZE * 16;
        trans.rx_buffer = recv_buffer;
        // Realizar la transacción SPI
        esp_err_t ret = spi_device_transmit(spi, &trans);
        if (ret == ESP_OK) {
            //for (int i = 0; i < BUFFER_SIZE; i++) {
            //    ESP_LOGI(TAG, "Dato recibido: %u", recv_buffer[i]);
            //}
            xSemaphoreGive(spiDataReady); // Notificar que los datos están listos
        } else {
            ESP_LOGI(TAG, "Error en la transmisión SPI: %d", ret);
        }
    }
}

// Crear y configurar el socket
int create_socket() {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGI(TAG, "No se pudo crear el socket: errno %d", errno);
        return -1;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGI(TAG, "No se pudo enlazar el socket: errno %d", errno);
        close(sock);
        return -1;
    }

    err = listen(sock, 1);
    if (err != 0) {
        ESP_LOGI(TAG, "Error al iniciar escucha en el socket: errno %d", errno);
        close(sock);
        return -1;
    }

    return sock;
}

// Tarea para enviar datos por TCP directamente desde el buffer DMA
void tcp_send_task(void *arg) {
    while (1) {
        ESP_LOGI(TAG, "Esperando conexión...");
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int new_socket = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
        if (new_socket < 0) {
            ESP_LOGI(TAG, "No se pudo aceptar la conexión: errno %d", errno);
            continue;
        }

        ESP_LOGI(TAG, "Conexión establecida.");
        is_connected = true;

        while (1) {
            // Esperar hasta que los datos estén listos
            if (xSemaphoreTake(spiDataReady, portMAX_DELAY) == pdTRUE) {
                // Enviar los datos directamente desde el buffer DMA
                ssize_t sent = send(new_socket, recv_buffer, BUFFER_SIZE * sizeof(uint16_t), 0);
                if (sent < 0) {
                    ESP_LOGI(TAG, "Error al enviar datos: errno %d", errno);
                    break; // Salir del bucle para cerrar el socket
                }
            }
        }

        close(new_socket);
        ESP_LOGI(TAG, "Cliente desconectado.");
        is_connected = false;
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_softap();
    my_spi_master_initialize();
    sock = create_socket();

    // Crear semáforo para coordinar la disponibilidad de datos
    spiDataReady = xSemaphoreCreateBinary();
    if (spiDataReady == NULL) {
        ESP_LOGI(TAG, "No se pudo crear el semáforo");
        return;
    }

    // Crear las tareas en diferentes cores
    xTaskCreatePinnedToCore(tcp_send_task, "TCP Send Task", 8000, NULL, 5, NULL, 1); // Core 1
    xTaskCreatePinnedToCore(spi_receive_task, "SPI Receive Task", 8000, NULL, 5, NULL, 0); // Core 0
}
