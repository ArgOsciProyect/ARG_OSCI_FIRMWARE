/**
 * @file network.c
 * @brief Implementation of network and WiFi functionality for ESP32
 * oscilloscope
 */

#include "network.h"
#include "globals.h"

static const char *TAG = "NETWORK";

void wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi in AP+STA mode");

    // Create default AP netif if it doesn't exist
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    // Create default STA netif if it doesn't exist
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure access point settings
    wifi_config_t wifi_config = {
        .ap = {.ssid = WIFI_SSID,
               .ssid_len = strlen(WIFI_SSID),
               .password = WIFI_PASSWORD,
               .max_connection = MAX_STA_CONN,
               .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    // Set open authentication if no password provided
    if (strlen(WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set WiFi mode to both AP and STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized successfully, SSID: %s", WIFI_SSID);
}

esp_err_t safe_close(int sock)
{
    if (sock < 0) {
        return ESP_OK; // Already closed
    }

    ESP_LOGI(TAG, "Attempting to safely close socket %d", sock);
    bool force_close = false;
    esp_err_t ret = ESP_OK;

    // Try graceful shutdown first with linger option
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 30; // 30 second timeout

    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_LINGER on socket %d", sock);
        force_close = true;
    }

    // Shutdown both sending and receiving if not forcing close
    if (!force_close && shutdown(sock, SHUT_RDWR) < 0) {
        ESP_LOGW(TAG, "Shutdown failed for socket %d, errno %d", sock, errno);
        force_close = true;
    }

    // If graceful shutdown failed, force immediate close
    if (force_close) {
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;

        if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0) {
            ESP_LOGE(TAG, "Failed to set immediate close on socket %d", sock);
        }
    }

    // Close the socket
    if (close(sock) < 0) {
        ESP_LOGE(TAG, "Close failed for socket %d, errno %d", sock, errno);
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Successfully closed socket %d", sock);
    }

    return ret;
}

esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info)
{
    ESP_LOGI(TAG, "Getting AP IP info");

    // Get AP interface IP information
    esp_err_t ret = esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), ip_info);

    if (ret != ESP_OK || ip_info->ip.addr == 0) {
        ESP_LOGE(TAG, "Failed to get IP address of AP");
        return ESP_FAIL;
    }

    char ip_str[16];
    esp_ip4addr_ntoa(&ip_info->ip, ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "AP IP address: %s", ip_str);

    return ESP_OK;
}

esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info)
{
    ESP_LOGI(TAG, "Waiting for IP address assignment in STA mode");

    // Poll for IP address with timeout
    for (int i = 0; i < 10; i++) {
        ESP_LOGI(TAG, "Waiting for IP address... attempt %d/10", i + 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ip_info) == ESP_OK &&
            ip_info->ip.addr != 0) {
            char ip_str[16];
            esp_ip4addr_ntoa(&ip_info->ip, ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "IP address obtained: %s", ip_str);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Failed to get IP address (timeout)");
    return ESP_FAIL;
}

esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info)
{
    ESP_LOGI(TAG, "Creating and binding socket");

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Prepare socket address
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip_info->ip.addr;
    addr.sin_port = htons(0); // Let the OS assign a port

    // Bind to the socket
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        safe_close(sock);
        return ESP_FAIL;
    }

    // Start listening
    if (listen(sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        safe_close(sock);
        return ESP_FAIL;
    }

    // Store the socket in the global variable
    new_sock = sock;

    // Get assigned port number
    struct sockaddr_in bound_addr;
    socklen_t addr_len = sizeof(bound_addr);
    if (getsockname(sock, (struct sockaddr *)&bound_addr, &addr_len) == 0) {
        char ip_str[16];
        inet_ntoa_r(bound_addr.sin_addr, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Socket bound to %s:%d", ip_str, ntohs(bound_addr.sin_port));
    }

    return ESP_OK;
}

void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record)
{
    // Check if this SSID is already in the array
    bool ssid_exists = false;
    cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        cJSON *ssid = cJSON_GetObjectItem(item, "SSID");
        if (ssid && strcmp(ssid->valuestring, (char *)ap_record->ssid) == 0) {
            ssid_exists = true;
            break;
        }
    }

    // Add if SSID is not empty and not already in the array
    if (!ssid_exists && strlen((char *)ap_record->ssid) > 0) {
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "SSID", (char *)ap_record->ssid);
        cJSON_AddItemToArray(root, item);
    }
}

cJSON *scan_and_get_ap_records(uint16_t *num_networks)
{
    ESP_LOGI(TAG, "Scanning for WiFi networks");

    // Configure scan parameters
    wifi_scan_config_t scan_config = {.ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true};

    // Start WiFi scan
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Get number of networks found
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(num_networks));
    ESP_LOGI(TAG, "Found %d networks", *num_networks);

    // Allocate memory for scan results
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * (*num_networks));
    if (!ap_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return NULL;
    }

    // Get scan results
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(num_networks, ap_records));

    // Create JSON array for unique SSIDs
    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON array");
        free(ap_records);
        return NULL;
    }

    // Add unique SSIDs to JSON array
    for (int i = 0; i < *num_networks; i++) {
        add_unique_ssid(root, &ap_records[i]);
    }

    // Clean up
    free(ap_records);

    return root;
}