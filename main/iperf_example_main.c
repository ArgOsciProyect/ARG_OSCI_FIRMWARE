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
#include <math.h>

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASS "12345678"
#define TCP_PORT 8080
#define MAX_CLIENTS 100
#define ADC_CHANNEL ADC_CHANNEL_5
#define SAMPLE_RATE_HZ 2000000 // 2 MHz
#define BUF_SIZE 4096

static const char *TAG = "ARG_OSCI";
int client_socket = -1;
bool client_connected = false;
int sock;
adc_continuous_handle_t adc_handle;

EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

int read_miss_count = 0;

void start_adc_sampling() {
    ESP_LOGI(TAG, "Starting ADC sampling");

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,  
        .channel = ADC_CHANNEL,   
        .bit_width = ADC_BITWIDTH_9
    };

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE*6,
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

void adc_receive_and_send_task(void *pvParameters) {
    struct sockaddr_in destAddr;
    socklen_t addr_len = sizeof(destAddr);
    char addr_str[128];
    uint8_t buffer[BUF_SIZE];
    uint32_t len;

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
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            if (ret == ESP_OK && len > 0) {
                if (send(client_socket, buffer, len, 0) < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    client_connected = false;
                }
            } else {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10) {  
                    ESP_LOGE(TAG, "Critical ADC data loss detected.");
                    read_miss_count = 0;
                }
            }
        }

        stop_adc_sampling();
        close(client_socket);
        ESP_LOGI(TAG, "Client disconnected");
    }
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

    xTaskCreate(adc_receive_and_send_task, "adc_receive_and_send_task", BUF_SIZE * 1.3, NULL, 5, NULL);

    ESP_LOGI(TAG, "Setup completed, waiting for clients...");
}
