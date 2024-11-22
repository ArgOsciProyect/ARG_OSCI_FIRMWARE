#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/sockets.h>

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASSWORD "password123"
#define MAX_STA_CONN 4
#define PORT 8080

static const char *TAG = "ESP32_AP";

void wifi_init() {

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASSWORD,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void scan_wifi_and_send(int sock) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t num_networks = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&num_networks));
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * num_networks);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_networks, ap_records));

    char buffer[1024];
    int len = 0;
    for (int i = 0; i < num_networks; i++) {
        len += snprintf(buffer + len, sizeof(buffer) - len,
                        "SSID:%s,\n",
                        ap_records[i].ssid);
        if (len >= sizeof(buffer)) {
            break;
        }
    }
    send(sock, buffer, strlen(buffer), 0);
    free(ap_records);
}


bool parse_ssid_password(const char *input, char *ssid, char *password) {
    const char *ssid_start = strstr(input, "SSID: ");
    const char *password_start = strstr(input, "Password: ");

    if (!ssid_start || !password_start) {
        return false;
    }

    ssid_start += strlen("SSID: ");
    password_start += strlen("Password: ");

    const char *ssid_end = strchr(ssid_start, ',');
    if (!ssid_end) {
        return false;
    }

    size_t ssid_len = ssid_end - ssid_start;
    if (ssid_len >= 32) {
        return false;
    }

    strncpy(ssid, ssid_start, ssid_len);
    ssid[ssid_len] = '\0';

    strncpy(password, password_start, 63);
    password[63] = '\0';

    return true;
}

void tcp_server_task(void *pvParameters) {
    wifi_init(); // Inicializar Wi-Fi

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr("192.168.4.1"), // IP fija del AP
        .sin_port = htons(PORT)
    };

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
    }

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
    }

    if (listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }

        char recv_buf[128];
        memset(recv_buf, 0, sizeof(recv_buf));
        int len = recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error during recv: errno %d", errno);
            close(client_sock);
            continue;
        }

        recv_buf[len] = '\0'; // Null-terminate the received string
        ESP_LOGI(TAG, "Len %d", len);
        ESP_LOGI(TAG, "Received %s", recv_buf);
        if (strcmp(recv_buf, "Ext_AP") == 0) {
            scan_wifi_and_send(client_sock);

            while (1) {
                memset(recv_buf, 0, sizeof(recv_buf));
                len = recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
                if (len < 0) {
                    ESP_LOGE(TAG, "Error during recv: errno %d", errno);
                    break;
                }

                recv_buf[len] = '\0'; // Null-terminate the received string
                ESP_LOGI(TAG, "Received after mode: %s", recv_buf);
                char ssid[32], password[64];
                memset(ssid, 0, sizeof(ssid));
                memset(password, 0, sizeof(password));

                if (parse_ssid_password(recv_buf, ssid, password)) {
                    // Attempt to connect to the Wi-Fi network
                    ESP_LOGI(TAG, "Connecting to %s...", ssid);
                    wifi_config_t wifi_config = {
                        .sta = {
                            .ssid = "",
                            .password = ""
                        },
                    };
                    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
                    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

                    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
                    ESP_ERROR_CHECK(esp_wifi_start());
                    ESP_ERROR_CHECK(esp_wifi_connect());

                    // Create a new socket in the new network
                    int new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
                    struct sockaddr_in new_addr;
                    new_addr.sin_family = AF_INET;
                    new_addr.sin_addr.s_addr = htonl(INADDR_ANY);
                    new_addr.sin_port = htons(0); // Let the system choose the port

                    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
                        ESP_LOGE(TAG, "New socket unable to bind: errno %d", errno);
                        close(new_sock);
                        break;
                    }

                    socklen_t new_addr_len = sizeof(new_addr);
                    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
                        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
                        close(new_sock);
                        break;
                    }

                    char ip_str[16];
                    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
                    int new_port = ntohs(new_addr.sin_port);

                    send(client_sock, ip_str, strlen(ip_str), 0);
                    send(client_sock, &new_port, sizeof(new_port), 0);

                    // Disable AP mode
                    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

                    close(new_sock);
                    break;
                } else {
                    ESP_LOGE(TAG, "Invalid SSID or Password format");
                }
            }
        }

        close(client_sock);
    }

    close(listen_sock);
    vTaskDelete(NULL);
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

    xTaskCreate(tcp_server_task, "tcp_server", 4096*2, NULL, 5, NULL);
}
