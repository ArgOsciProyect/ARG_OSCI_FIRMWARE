#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_adc/adc_continuous.h"
#include "esp_netif.h"

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASS "12345678"
#define TCP_PORT 8080
#define MAX_CLIENTS 1
#define ADC_CHANNEL ADC_CHANNEL_3
#define SAMPLE_RATE_HZ 2000000 // 2 MHz
#define ADC_RESOLUTION 9 // 9-bit resolution
#define BUF_SIZE 4096
#define UNPACKED_BUF_SIZE (BUF_SIZE / sizeof(adc_digi_output_data_t))

static const char *TAG = "ARG_OSCI";
int client_socket = -1;
bool client_connected = false;
int sock;
adc_continuous_handle_t adc_handle;

EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// Buffer compartido y semáforo
uint16_t unpacked_buffer[UNPACKED_BUF_SIZE];
SemaphoreHandle_t buffer_semaphore;

// Configuración y muestreo continuo del ADC
void start_adc_sampling() {
    ESP_LOGI(TAG, "Starting ADC sampling");

    // Configuración del patrón para el canal y resolución
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_0,  // Sin atenuación
        .channel = ADC_CHANNEL,   // Canal definido
        .bit_width = ADC_BITWIDTH_9  // Resolución de 9 bits
    };

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096, // Tamaño del buffer
        .conv_frame_size = 512,     // Tamaño de los frames de conversión
    };

    // Crear el handle del ADC continuo
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    // Configuración del ADC continuo
    adc_continuous_config_t continuous_config = {
        .pattern_num = 1,                     // Número de patrones
        .adc_pattern = &adc_pattern,          // Dirección del patrón
        .sample_freq_hz = SAMPLE_RATE_HZ,     // Frecuencia de muestreo a 2 MHz
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,  // Modo de conversión
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1 // Formato de salida
    };

    // Aplicar la configuración del ADC
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &continuous_config));

    // Iniciar la conversión ADC continua
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

// Detener el muestreo del ADC
void stop_adc_sampling() {
    ESP_LOGI(TAG, "Stopping ADC sampling");
    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}

// Tarea de desempaquetado
void unpack_data_task(void *pvParameters) {
    uint8_t buffer[BUF_SIZE];
    uint32_t len;

    while (1) {
        if (client_connected) {
            // Leer datos del ADC
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            if (ret == ESP_OK && len > 0) {
                // Desempaquetar datos y almacenarlos en el buffer compartido
                xSemaphoreTake(buffer_semaphore, portMAX_DELAY);
                uint16_t *unpacked_ptr = unpacked_buffer;
                for (uint32_t i = 0; i < len; i += 6) { // Asumiendo que cada bloque de datos tiene 6 bytes
                    // Imprimir los valores de los 6 bytes
                    ESP_LOGI(TAG, "Bytes: %02X %02X %02X %02X %02X %02X",
                             buffer[i], buffer[i + 1], buffer[i + 2], buffer[i + 3], buffer[i + 4], buffer[i + 5]);
                    sleep(100);
                    // Aquí puedes decidir qué bytes usar basándote en la salida
                }
                xSemaphoreGive(buffer_semaphore);
            } else {
                ESP_LOGE(TAG, "Error reading ADC data: %d", ret);
            }
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS); // Esperar antes de verificar nuevamente
        }
    }
}

// Tarea de envío
void send_data_task(void *pvParameters) {
    struct sockaddr_in destAddr;
    socklen_t addr_len = sizeof(destAddr);
    char addr_str[128];

    while (1) {
        client_socket = accept(sock, (struct sockaddr *)&destAddr, &addr_len);
        if (client_socket < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
        ESP_LOGI(TAG, "Client connected: %s", addr_str);
        client_connected = true;

        // Inicia muestreo ADC
        start_adc_sampling();

        // Envío de datos desempaquetados
        while (client_connected) {
            xSemaphoreTake(buffer_semaphore, portMAX_DELAY);
            if (send(client_socket, unpacked_buffer, sizeof(unpacked_buffer), 0) < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                client_connected = false;
            }
            xSemaphoreGive(buffer_semaphore);
        }

        // Detener muestreo al desconectarse
        stop_adc_sampling();
        close(client_socket);
        ESP_LOGI(TAG, "Client disconnected");
    }
}

// Configuración Wi-Fi (AP)
void wifi_init_softap() {
    ESP_LOGI(TAG, "Setting up AP...");

    esp_netif_init();
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    assert(netif);

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);
    esp_netif_dhcps_start(netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP initialized with SSID:%s", WIFI_SSID);
}

// Función principal
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

    // Inicializar Wi-Fi
    wifi_init_softap();

    // Configurar socket
    struct sockaddr_in server_addr;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        return;
    }

    if (listen(sock, MAX_CLIENTS) < 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(sock);
        return;
    }

    // Crear semáforo para el buffer compartido
    buffer_semaphore = xSemaphoreCreateMutex();

    // Crear tareas para desempaquetar y enviar datos
    xTaskCreatePinnedToCore(unpack_data_task, "unpack_data_task", 15000, NULL, 5, NULL, 0); // Ejecutar en el núcleo 0
    xTaskCreatePinnedToCore(send_data_task, "send_data_task", 15000, NULL, 5, NULL, 1); // Ejecutar en el núcleo 1
}