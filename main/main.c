#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/sockets.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <esp_netif.h> // Incluir la biblioteca correcta
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASSWORD "password123"
#define MAX_STA_CONN 4
#define PORT 8080

static const char *TAG = "ESP32_AP";
static int new_sock = -1;
static httpd_handle_t second_server = NULL;
static TaskHandle_t socket_task_handle = NULL;

void wifi_init() {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

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

void socket_task(void *pvParameters) {
    while (1) {
        if (new_sock == -1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(new_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            close(new_sock);
            new_sock = -1;
            continue;
        }
        else{
            ESP_LOGI(TAG, "Client connected, IP: %s, Port: %d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }

        char rx_buffer[128];
        while (1) {
            int len = send(client_sock, "Hello, world!", 14, 0);
            if (len < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed");
                break;
            }
            ESP_LOGI(TAG, "Sent %d bytes", len);
            vTaskDelay(1000 / portTICK_PERIOD_MS);            
        }

        close(client_sock);
    }
}

esp_err_t scan_wifi_handler(httpd_req_t *req) {
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
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < num_networks; i++) {
        // Verificar si el SSID ya existe en el JSON
        bool ssid_exists = false;
        cJSON *item;
        cJSON_ArrayForEach(item, root) {
            cJSON *ssid = cJSON_GetObjectItem(item, "SSID");
            if (ssid && strcmp(ssid->valuestring, (char *)ap_records[i].ssid) == 0) {
                ssid_exists = true;
                break;
            }
        }
        // Agregar el SSID solo si no existe
        if (!ssid_exists && strlen((char *)ap_records[i].ssid) > 0) {
            item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "SSID", (char *)ap_records[i].ssid);
            cJSON_AddItemToArray(root, item);
        }
    }
    const char *json_response = cJSON_Print(root);
    httpd_resp_send(req, json_response, strlen(json_response));
    free(ap_records);
    cJSON_Delete(root);
    free((void *)json_response);
    return ESP_OK;
}

esp_err_t test_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Test handler called");
    cJSON *root = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "Test", "John Doe");
    cJSON_AddItemToArray(root, item);
    const char *json_response = cJSON_Print(root);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(root);
    free((void *)json_response);
    return ESP_OK;
}

httpd_handle_t start_second_webserver(void) {
    // Detener el servidor existente si ya está en ejecución
    if (second_server != NULL) {
        httpd_stop(second_server);
        second_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    if (httpd_start(&second_server, &config) == ESP_OK) {
        httpd_uri_t test_uri = {
            .uri = "/test",
            .method = HTTP_GET,
            .handler = test_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(second_server, &test_uri);
    }

    return second_server;
}

esp_err_t connect_wifi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Connecting to Wi-Fi network");
    char content[200];
    int ret = httpd_req_recv(req, content, sizeof(content));
    ESP_LOGI(TAG, "Received content: %s", content);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON *ssid = cJSON_GetObjectItem(root, "SSID");
    cJSON *password = cJSON_GetObjectItem(root, "Password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        httpd_resp_send_408(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = ""
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid->valuestring, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password->valuestring, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // Mantener el modo APSTA
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    cJSON_Delete(root);

    // Esperar a que se asigne una IP
    esp_netif_ip_info_t ip_info;
    for (int i = 0; i < 10; i++) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            break;
        }
    }
    if (ip_info.ip.addr == 0) {
        ESP_LOGE(TAG, "Failed to get IP address");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Detener el socket y el segundo servidor web si ya existen
    if (new_sock != -1) {
        close(new_sock);
        new_sock = -1;
    }
    if (second_server != NULL) {
        httpd_stop(second_server);
        second_server = NULL;
    }

    // Crear un socket en la red local del AP externo
    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info.ip.addr; // Usar la IP asignada al ESP32 en modo STA
    new_addr.sin_port = htons(0); // Dejar que el sistema elija el puerto automáticamente

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Obtener el puerto y la IP asignados automáticamente
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "IP", ip_str);
    cJSON_AddNumberToObject(response, "Port", new_port);
    const char *json_response = cJSON_Print(response);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(response);
    free((void *)json_response);

    // Iniciar el segundo servidor web en la red STA
    second_server = start_second_webserver();

    return ESP_OK;
}

esp_err_t internal_mode_handler(httpd_req_t *req) {
    // Obtener la IP del AP
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        ESP_LOGE(TAG, "Failed to get IP address of AP");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Crear un socket en la red local del AP
    if (new_sock != -1) {
        close(new_sock);
        new_sock = -1;
    }

    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info.ip.addr; // Usar la IP del AP
    new_addr.sin_port = htons(0); // Dejar que el sistema elija el puerto automáticamente

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Obtener el puerto y la IP asignados automáticamente
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "IP", ip_str);
    cJSON_AddNumberToObject(response, "Port", new_port);
    const char *json_response = cJSON_Print(response);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(response);
    free((void *)json_response);

    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 81;
    config.ctrl_port = 32767;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t scan_wifi_uri = {
            .uri = "/scan_wifi",
            .method = HTTP_GET,
            .handler = scan_wifi_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &scan_wifi_uri);

        httpd_uri_t connect_wifi_uri = {
            .uri = "/connect_wifi",
            .method = HTTP_POST,
            .handler = connect_wifi_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &connect_wifi_uri);

        httpd_uri_t internal_mode_uri = {
            .uri = "/internal_mode",
            .method = HTTP_GET,
            .handler = internal_mode_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &internal_mode_uri);
    }

    return server;
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

    wifi_init(); // Inicializar Wi-Fi

    start_webserver(); // Iniciar servidor web

    // Crear la tarea para manejar el socket en el núcleo 1
    xTaskCreatePinnedToCore(socket_task, "socket_task", 4096, NULL, 5, &socket_task_handle, 1);
}