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
#include "driver/dac_cosine.h"  // Cambiamos la librería para el DAC
#include "esp_timer.h"
#include <math.h>

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASS "12345678"
#define TCP_PORT 8080
#define MAX_CLIENTS 1
#define ADC_CHANNEL ADC_CHANNEL_0
#define SAMPLE_RATE_HZ 2000000 // 2 MHz
#define ADC_RESOLUTION 9 // 9-bit resolution
#define BUF_SIZE 4096

// Define for 8 bits transfer
//#define TRANSFER_BITS_8

#ifdef TRANSFER_BITS_8
    #define UNPACKED_BUF_SIZE (BUF_SIZE / sizeof(uint8_t))
    uint8_t unpacked_buffer[UNPACKED_BUF_SIZE];
#else
    #define UNPACKED_BUF_SIZE (BUF_SIZE / sizeof(adc_digi_output_data_t))
    uint16_t unpacked_buffer[UNPACKED_BUF_SIZE];
#endif

static const char *TAG = "ARG_OSCI";
int client_socket = -1;
bool client_connected = false;
int sock;
adc_continuous_handle_t adc_handle;

EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

SemaphoreHandle_t buffer_semaphore;

int read_miss_count = 0;

void start_adc_sampling() {
    ESP_LOGI(TAG, "Starting ADC sampling");

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_0,  
        .channel = ADC_CHANNEL,   
        .bit_width = ADC_BITWIDTH_9
    };

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096*2,
        .conv_frame_size = 128,
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_continuous_config_t continuous_config = {
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
        .sample_freq_hz = SAMPLE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
    };

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &continuous_config));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

void stop_adc_sampling() {
    ESP_LOGI(TAG, "Stopping ADC sampling");
    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}
// Tarea de desempaquetado
void unpack_data_task(void *pvParameters) {
    uint8_t buffer[BUF_SIZE];
    uint32_t len;
    int read_miss_count = 0;
    while (1) {
        if (client_connected) {
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            if (ret == ESP_OK && len > 0) {
                read_miss_count = 0;

                xSemaphoreTake(buffer_semaphore, portMAX_DELAY);
                #ifdef TRANSFER_BITS_8
                    uint8_t *unpacked_ptr = unpacked_buffer;
                    for (uint32_t i = 0; i < len; i += sizeof(adc_digi_output_data_t)) {
                        adc_digi_output_data_t *adc_data = (adc_digi_output_data_t *)&buffer[i];
                        *unpacked_ptr++ = (uint8_t)(adc_data->type1.data >> 1);
                    }
                #else
                    uint16_t *unpacked_ptr = unpacked_buffer;
                    for (uint32_t i = 0; i < len; i += sizeof(adc_digi_output_data_t)) {
                        adc_digi_output_data_t *adc_data = (adc_digi_output_data_t *)&buffer[i];
                        *unpacked_ptr++ = adc_data->type1.data;
                    }
                #endif
                xSemaphoreGive(buffer_semaphore);
            } else {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10) {  
                    ESP_LOGE(TAG, "Critical ADC data loss detected.");
                    read_miss_count = 0;
                }
            }
        } else {
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Esperar 1 segundo antes de verificar nuevamente
        }
    }
}

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

        start_adc_sampling();

        while (client_connected) {
            xSemaphoreTake(buffer_semaphore, portMAX_DELAY);
            #ifdef TRANSFER_BITS_8
                if (send(client_socket, unpacked_buffer, sizeof(unpacked_buffer), 0) < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    client_connected = false;
                }
            #else
                if (send(client_socket, unpacked_buffer, sizeof(unpacked_buffer), 0) < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    client_connected = false;
                }
            #endif
            xSemaphoreGive(buffer_semaphore);
        }

        stop_adc_sampling();
        close(client_socket);
        ESP_LOGI(TAG, "Client disconnected");
    }
}



void init_sine_wave() {
    dac_cosine_handle_t chan0_handle;
    dac_cosine_config_t cos0_cfg = {
        .chan_id = DAC_CHAN_0,
        .freq_hz = 10000,             // Frecuencia de la señal senoidal en Hz
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true, 
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos0_cfg, &chan0_handle));
    ESP_ERROR_CHECK(dac_cosine_start(chan0_handle));
}

void dac_sine_wave_task(void *pvParameters) {
    init_sine_wave();
    vTaskDelete(NULL);  // Finalizamos la tarea una vez que la señal está configurada
}

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
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_AP, WIFI_BW_HT40));
    ESP_ERROR_CHECK(esp_wifi_config_80211_tx_rate(ESP_IF_WIFI_AP, WIFI_PHY_RATE_MCS7_SGI));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP initialized with SSID:%s", WIFI_SSID);
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

    wifi_init_softap();

    struct sockaddr_in server_addr;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    int err = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        return;
    }

    err = listen(sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return;
    }

    buffer_semaphore = xSemaphoreCreateMutex();

    xTaskCreate(dac_sine_wave_task, "dac_sine_wave_task", 2048, NULL, 5, NULL);
    xTaskCreate(unpack_data_task, "unpack_data_task", 15000, NULL, 5, NULL);
    xTaskCreate(send_data_task, "send_data_task", 15000, NULL, 5, NULL);


    ESP_LOGI(TAG, "Setup completed, waiting for clients...");
}
