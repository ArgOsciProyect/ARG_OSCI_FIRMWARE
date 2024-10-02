#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_system.h"  // Include the header for esp_cpu_get_cycle_count()
#include "esp_system.h"

#define WIFI_SSID_STA "IPLAN-1_D"
#define WIFI_PASS_STA "santiagojoaquin"
#define WIFI_SSID_AP "ESP32"
#define WIFI_PASS_AP "12345678"
#define MAX_STA_CONN 1
#define PORT 8080
#define DATA_SIZE 1440  // Tamaño del array para un período completo
#define PI 3.14159265358979323846
#define SAMPLE_RATE 2000000  // Frecuencia de muestreo en Hz (20 kHz)
#define DELAY_US (0.5*DATA_SIZE)  // Delay in microseconds for 2 MHz rate
#define SIGNAL_FREQUENCY 500000  // Frecuencia de la señal en Hz (1 kHz)

// Define to select protocol (TCP or UDP)
#define USE_TCP

// Define to select mode (AP or STA)
#define USE_AP

#define ENABLE_COMPRESSION  // Definir para habilitar compresión

static const char *TAG = "wifi_example";
const int WIFI_CONNECTED_BIT = BIT0;

QueueHandle_t data_queue;
volatile bool transmission_active = false;

#ifdef USE_AP
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
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(19));  // Set max transmit power to 19 dBm

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", WIFI_SSID_AP, WIFI_PASS_AP);
}
#else
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID_STA,
            .password = WIFI_PASS_STA,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    // Set the required Wi-Fi configurations
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT40));  // Set 40 MHz bandwidth
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(19));  // Set max transmit power to 19 dBm

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished. SSID:%s password:%s", WIFI_SSID_STA, WIFI_PASS_STA);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
    } else {
        ESP_LOGE(TAG, "Failed to connect to AP");
    }
}
#endif

void generate_sine_wave_task(void *pvParameters)
{
    uint16_t *data = (uint16_t *)malloc(DATA_SIZE * sizeof(uint16_t));
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for data");
        vTaskDelete(NULL);
        return;
    }

    for (int i = 0; i < DATA_SIZE; i++) {
        data[i] = (uint16_t)((sin(2 * PI * SIGNAL_FREQUENCY * i / SAMPLE_RATE) + 1) * 2047.5);
    }

    while (1) {
        if (transmission_active) {
            if (xQueueSend(data_queue, data, portMAX_DELAY) != pdPASS) {
                ESP_LOGE(TAG, "Failed to send data to queue");
            }
            esp_rom_delay_us(DELAY_US);
        }
    }

    free(data);
}

#ifdef ENABLE_COMPRESSION
void pack_12bit_data_stream(uint16_t *input, uint8_t *output, size_t num_samples) {
    size_t output_index = 0;
    uint32_t bit_buffer = 0;  // Buffer de bits para almacenar las muestras
    int bit_count = 0;        // Contador de bits almacenados

    for (size_t i = 0; i < num_samples; i++) {
        bit_buffer |= ((uint32_t)(input[i] & 0x0FFF) << bit_count);  // Carga los 12 bits en el buffer
        bit_count += 12;  // Incrementa el contador de bits

        // Mientras tengamos al menos 8 bits en el buffer, extraemos un byte
        while (bit_count >= 8) {
            output[output_index++] = (uint8_t)(bit_buffer & 0xFF);  // Toma los 8 bits más bajos
            bit_buffer >>= 8;  // Desplaza el buffer
            bit_count -= 8;    // Reduce el contador de bits
        }
    }

    // Si quedan bits residuales en el buffer al final, los almacenamos en el siguiente byte
    if (bit_count > 0) {
        output[output_index++] = (uint8_t)(bit_buffer & 0xFF);
    }
}

#endif

void server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr;
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        transmission_active = true;
        uint16_t *data = (uint16_t *)malloc(DATA_SIZE * sizeof(uint16_t));
        uint8_t *compressed_data = (uint8_t *)malloc((DATA_SIZE * 3) / 2);

        while (1) {
            if (xQueueReceive(data_queue, data, portMAX_DELAY) == pdPASS) {
#ifdef ENABLE_COMPRESSION
                pack_12bit_data_stream(data, compressed_data, DATA_SIZE);
                int written = send(sock, compressed_data, (DATA_SIZE * 3) / 2, 0);
#else
                int written = send(sock, data, DATA_SIZE * sizeof(uint16_t), 0);
#endif
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
            }
        }

        transmission_active = false;

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }

        free(data);
        free(compressed_data);
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifdef USE_AP
    wifi_init_softap();
#else
    wifi_init_sta();
#endif

    data_queue = xQueueCreate(10, DATA_SIZE * sizeof(uint16_t));
    if (data_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // Reducir el tamaño de la pila ya que data ahora se asigna dinámicamente
    xTaskCreatePinnedToCore(generate_sine_wave_task, "generate_sine_wave_task", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(server_task, "server_task", 4096, NULL, 5, NULL, 0);
}