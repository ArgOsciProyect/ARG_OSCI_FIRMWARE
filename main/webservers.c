/**
 * @file webservers.c
 * @brief Implementation of HTTP server handlers for ESP32 oscilloscope
 */

#include "webservers.h"
#include "acquisition.h"
#include "crypto.h"
#include "data_transmission.h"
#include "globals.h"
#include "network.h"

static const char *TAG = "WEBSERVER";

// Definition of global variables declared as extern in globals.h
httpd_handle_t second_server = NULL;
int new_sock = -1;

esp_err_t config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Config handler called");
    httpd_resp_set_type(req, "application/json");

    cJSON *config = cJSON_CreateObject();
    if (config == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddNumberToObject(config, "sampling_frequency", get_sampling_frequency());
    cJSON_AddNumberToObject(config, "bits_per_packet", get_bits_per_packet());
    cJSON_AddNumberToObject(config, "data_mask", get_data_mask());
    cJSON_AddNumberToObject(config, "channel_mask", get_channel_mask());
    cJSON_AddNumberToObject(config, "useful_bits", get_useful_bits());
    cJSON_AddNumberToObject(config, "samples_per_packet", get_samples_per_packet());
    cJSON_AddNumberToObject(config, "dividing_factor", dividing_factor());
    cJSON_AddNumberToObject(config, "discard_head", get_discard_head());
    cJSON_AddNumberToObject(config, "discard_trailer", get_discard_trailer());
    cJSON_AddNumberToObject(config, "max_bits", get_max_bits());
    cJSON_AddNumberToObject(config, "mid_bits", get_mid_bits());

    // Create the voltage scales array
    cJSON *voltage_scales_array = cJSON_CreateArray();
    if (voltage_scales_array != NULL) {
        const voltage_scale_t *scales = get_voltage_scales();
        int count = get_voltage_scales_count();

        // Add each scale to the array
        for (int i = 0; i < count; i++) {
            cJSON *scale = cJSON_CreateObject();
            if (scale != NULL) {
                cJSON_AddNumberToObject(scale, "baseRange", scales[i].baseRange);
                cJSON_AddStringToObject(scale, "displayName", scales[i].displayName);
                cJSON_AddItemToArray(voltage_scales_array, scale);
            }
        }

        cJSON_AddItemToObject(config, "voltage_scales", voltage_scales_array);
    }

    const char *response = cJSON_Print(config);
    esp_err_t ret = httpd_resp_send(req, response, strlen(response));

    free((void *)response);
    cJSON_Delete(config);

    return ret;
}

esp_err_t scan_wifi_handler(httpd_req_t *req)
{
    uint16_t num_networks = 0;
    cJSON *root = scan_and_get_ap_records(&num_networks);

    if (root == NULL) {
        return httpd_resp_send_500(req);
    }

    const char *json_response = cJSON_Print(root);
    if (json_response == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(root);
    free((void *)json_response);

    return ret;
}

esp_err_t test_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Test handler called");

    // Read POST data
    char content[600];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[received] = '\0';
    ESP_LOGI(TAG, "Received content: %s", content);

    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGI(TAG, "Failed to parse JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get encrypted message and copy it
    cJSON *encrypted_msg = cJSON_GetObjectItem(root, "word");
    if (!encrypted_msg || !cJSON_IsString(encrypted_msg)) {
        ESP_LOGI(TAG, "Failed to get encrypted message");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Copy the encrypted message since we will free root
    char *encrypted_copy = strdup(encrypted_msg->valuestring);
    if (!encrypted_copy) {
        ESP_LOGI(TAG, "Failed to copy encrypted message");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Free root since we no longer need it
    cJSON_Delete(root);

    // Decrypt message
    char decrypted[256];
    if (decrypt_base64_message(encrypted_copy, decrypted, sizeof(decrypted)) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to decrypt message");
        free(encrypted_copy);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Free encrypted copy since we no longer need it
    free(encrypted_copy);

    // Create response
    cJSON *response = cJSON_CreateObject();
    if (!response) {
        ESP_LOGI(TAG, "Failed to create response object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (!cJSON_AddStringToObject(response, "decrypted", decrypted)) {
        ESP_LOGI(TAG, "Failed to add string to response");
        cJSON_Delete(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *json_response = cJSON_Print(response);
    if (!json_response) {
        ESP_LOGI(TAG, "Failed to print response");
        cJSON_Delete(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send response
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    // Clean up in reverse order of allocation
    free((void *)json_response);
    cJSON_Delete(response);

    return ret;
}

esp_err_t trigger_handler(httpd_req_t *req)
{
    char content[100];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        return httpd_resp_send_408(req);
    }
    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get edge configuration
    cJSON *edge = cJSON_GetObjectItem(root, "trigger_edge");
    if (cJSON_IsString(edge)) {
        if (strcmp(edge->valuestring, "positive") == 0) {
            trigger_edge = 1;
        } else if (strcmp(edge->valuestring, "negative") == 0) {
            trigger_edge = 0;
        }
#ifdef USE_EXTERNAL_ADC
        if (mode == 1) {
            if (trigger_edge == 1) {
                // Configure for positive edge detection
                ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                             PCNT_CHANNEL_EDGE_ACTION_HOLD));
            } else {
                // Configure for negative edge detection
                ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                                             PCNT_CHANNEL_EDGE_ACTION_INCREASE));
            }
        }
#endif
    }

    // Get trigger percentage
    cJSON *trigger = cJSON_GetObjectItem(root, "trigger_percentage");
    if (!cJSON_IsNumber(trigger)) {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int percentage = (int)trigger->valuedouble;
    esp_err_t ret = ESP_OK;
    if (mode == 1) {
        ret = set_trigger_level(percentage);

        cJSON_Delete(root);

        if (ret != ESP_OK) {
            return httpd_resp_send_500(req);
        }
    }
    // Send success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "set_percentage", percentage);
    cJSON_AddStringToObject(response, "edge", trigger_edge ? "positive" : "negative");
    const char *json_response = cJSON_Print(response);

    httpd_resp_set_type(req, "application/json");
    ret = httpd_resp_send(req, json_response, strlen(json_response));

    free((void *)json_response);
    cJSON_Delete(response);

    return ret;
}

esp_err_t single_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Single handler called");

    // Set content type as JSON
    httpd_resp_set_type(req, "application/json");

    // Switch to single trigger mode
    esp_err_t ret = set_single_trigger_mode();
    if (ret != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    // Simple JSON response
    const char *response = "{\"mode\":\"Single\"}";
    return httpd_resp_send(req, response, strlen(response));
}

esp_err_t freq_handler(httpd_req_t *req)
{
#ifdef USE_EXTERNAL_ADC
    int final_freq;
#endif
    char content[100];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        return httpd_resp_send_408(req);
    }
    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        return httpd_resp_send_500(req);
    }

    // Get action (more/less)
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Pause to reduce potential crashes

#ifdef USE_EXTERNAL_ADC
    // Determine index based on action
    if (strcmp(action->valuestring, "less") == 0 && spi_index != 6) {
        spi_index++;
    }
    if (strcmp(action->valuestring, "more") == 0 && spi_index != 0) {
        spi_index--;
    }

    ESP_LOGI(TAG, "spi index: %d", spi_index);

    if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
        // Reinitialize SPI with new frequency
        ESP_LOGI(TAG, "Reinitializing SPI with new frequency: %lu", spi_matrix[spi_index][0]);
        ESP_ERROR_CHECK(spi_bus_remove_device(spi));

        spi_device_interface_config_t devcfg = {.clock_speed_hz = spi_matrix[spi_index][0],
                                                .mode = 0,
                                                .spics_io_num = PIN_NUM_CS,
                                                .queue_size = 7,
                                                .pre_cb = NULL,
                                                .post_cb = NULL,
                                                .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
                                                .cs_ena_pretrans = spi_matrix[spi_index][1],
                                                .input_delay_ns = spi_matrix[spi_index][2]};

        ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &devcfg, &spi));

        // Update MCPWM
        ESP_ERROR_CHECK(mcpwm_timer_set_period(timer, spi_matrix[spi_index][3]));

        // Update comparator value
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, spi_matrix[spi_index][4]));

        xSemaphoreGive(spi_mutex);
    }

    ESP_ERROR_CHECK(spi_device_get_actual_freq(spi, &final_freq));

    // Build response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "sampling_frequency", final_freq * SPI_FREQ_SCALE_FACTOR);
#else
    // Adjust divider for internal ADC
    if (strcmp(action->valuestring, "less") == 0 && adc_divider != 16) {
        adc_divider *= 2;
    }
    if (strcmp(action->valuestring, "more") == 0 && adc_divider != 1) {
        adc_divider /= 2;
    }
    adc_modify_freq = 1;

    // Build response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "sampling_frequency", get_sampling_frequency() / adc_divider);
#endif

    const char *json_response = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    // Cleanup
    free((void *)json_response);
    cJSON_Delete(response);
    cJSON_Delete(root);

    return ret;
}

esp_err_t reset_socket_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reset socket handler called");

#ifndef USE_EXTERNAL_ADC
    // Request socket task to pause ADC operations
    atomic_store(&wifi_operation_requested, 1);

    // Wait for acknowledgment with timeout
    int timeout_count = 0;
    while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_count++;
    }

    if (timeout_count >= 500) {
        ESP_LOGW(TAG, "Timeout waiting for socket task to acknowledge WiFi operation");
        // Continue anyway, but there might be issues
    }
#else
    // Signal socket task to close any client connections
    ESP_LOGI(TAG, "Requesting socket reset before resetting socket");
    request_socket_reset();
#endif

    // Get appropriate IP information based on request origin port
    esp_netif_ip_info_t ip_info;
    if (httpd_req_get_hdr_value_len(req, "Host") > 0) {
        char host[32];
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        // Verify if the request came from the AP server (port 81)
        bool is_ap_server = (strstr(host, ":81") != NULL);

        if (is_ap_server) {
            // If AP, use AP IP info
            if (get_ap_ip_info(&ip_info) != ESP_OK) {
#ifndef USE_EXTERNAL_ADC
                atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        } else {
            // If not AP, use STA IP info
            if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) != ESP_OK ||
                ip_info.ip.addr == 0) {
#ifndef USE_EXTERNAL_ADC
                atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        }
    } else {
        // Use AP mode by default if source cannot be determined
        if (get_ap_ip_info(&ip_info) != ESP_OK) {
#ifndef USE_EXTERNAL_ADC
            atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
    }

    // Close existing socket if there is one
    if (new_sock != -1) {
        safe_close(new_sock);
        new_sock = -1;
    }

    // Create and bind new socket
    if (create_socket_and_bind(&ip_info) != ESP_OK) {
#ifndef USE_EXTERNAL_ADC
        atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

#ifndef USE_EXTERNAL_ADC
    // Allow socket task to resume
    atomic_store(&wifi_operation_requested, 0);
#endif

    // Get socket information
    char ip_str[16];
    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    // Send response
    return send_internal_mode_response(req, ip_str, new_port);
}

esp_err_t normal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Normal handler called");

    // Set content type as JSON
    httpd_resp_set_type(req, "application/json");

    // Switch to continuous mode
    esp_err_t ret = set_continuous_mode();
    if (ret != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    // Simple JSON response
    const char *response = "{\"mode\":\"Normal\"}";
    return httpd_resp_send(req, response, strlen(response));
}

esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config)
{
    char content[KEYSIZE];
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

    cJSON *ssid_encrypted = cJSON_GetObjectItem(root, "SSID");
    cJSON *password_encrypted = cJSON_GetObjectItem(root, "Password");
    if (!cJSON_IsString(ssid_encrypted) || !cJSON_IsString(password_encrypted)) {
        httpd_resp_send_408(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Decrypt SSID
    char ssid_decrypted[512];
    if (decrypt_base64_message(ssid_encrypted->valuestring, ssid_decrypted, sizeof(ssid_decrypted)) != ESP_OK) {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Decrypt Password
    char password_decrypted[512];
    if (decrypt_base64_message(password_encrypted->valuestring, password_decrypted, sizeof(password_decrypted)) !=
        ESP_OK) {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy((char *)wifi_config->sta.ssid, ssid_decrypted, sizeof(wifi_config->sta.ssid));
    strncpy((char *)wifi_config->sta.password, password_decrypted, sizeof(wifi_config->sta.password));
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t connect_wifi_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Connecting to Wi-Fi network");
    wifi_config_t wifi_config = {
        .sta = {.ssid = "", .password = ""},
    };

    if (parse_wifi_credentials(req, &wifi_config) != ESP_OK) {
        return send_wifi_response(req, "", 0, false);
    }

#ifndef USE_EXTERNAL_ADC
    // Request socket task to pause ADC operations
    ESP_LOGI(TAG, "Pausing ADC operations for WiFi configuration");
    atomic_store(&wifi_operation_requested, 1);

    // Wait for acknowledgment with timeout
    int timeout_count = 0;
    while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_count++;
    }

    if (timeout_count >= 500) {
        ESP_LOGW(TAG, "Timeout waiting for socket task to acknowledge WiFi operation");
    }

    // Make sure the ADC is fully stopped and reset all flags
    if (atomic_load(&adc_is_running) || atomic_load(&adc_initializing)) {
        ESP_LOGI(TAG, "Stopping ADC for WiFi connection");
        stop_adc_sampling();

        // Extra delay to ensure ADC resources are fully released
        vTaskDelay(pdMS_TO_TICKS(500));

        // Ensure flags are cleared
        atomic_store(&adc_is_running, false);
        atomic_store(&adc_initializing, false);

        ESP_LOGI(TAG, "ADC flags reset for clean state");
    }

    // Extra safety delay before proceeding with network changes
    vTaskDelay(pdMS_TO_TICKS(200));
#else
    ESP_LOGI(TAG, "===== CRITICAL: Socket reset for connect_wifi =====");
    force_socket_cleanup(); // Use the stronger cleanup mechanism
    ESP_LOGI(TAG, "===== Socket reset for connect_wifi completed =====");
#endif

    // Now it's safe to modify WiFi configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();

#ifndef USE_EXTERNAL_ADC
    // Allow socket task to resume
    atomic_store(&wifi_operation_requested, 0);
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
        return send_wifi_response(req, "", 0, false);
    }

    esp_netif_ip_info_t ip_info;
    if (wait_for_ip(&ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address");
        return send_wifi_response(req, "", 0, false);
    }

    if (new_sock != -1) {
        safe_close(new_sock);
        new_sock = -1;
    }
    if (second_server != NULL) {
        httpd_stop(second_server);
        second_server = NULL;
    }

    if (create_socket_and_bind(&ip_info) != ESP_OK) {
        return send_wifi_response(req, "", 0, false);
    }

    char ip_str[16];
    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        return send_wifi_response(req, "", 0, false);
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    esp_err_t ret = send_wifi_response(req, ip_str, new_port, true);

    if (ret == ESP_OK) {
        second_server = start_second_webserver();
    }

    return ret;
}
esp_err_t internal_mode_handler(httpd_req_t *req)
{
#ifndef USE_EXTERNAL_ADC
    // Request socket task to pause ADC operations
    atomic_store(&wifi_operation_requested, 1);

    // Wait for acknowledgment with timeout
    int timeout_count = 0;
    while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_count++;
    }

    // Make sure ADC is fully stopped and its memory freed
    if (atomic_load(&adc_is_running) || atomic_load(&adc_initializing)) {
        ESP_LOGI(TAG, "Stopping ADC for internal mode transition");
        stop_adc_sampling();

        // Extra delay to ensure ADC resources are fully released
        vTaskDelay(pdMS_TO_TICKS(500));

        // Ensure flags are cleared
        atomic_store(&adc_is_running, false);
        atomic_store(&adc_initializing, false);

        ESP_LOGI(TAG, "ADC flags reset for clean state");
    }

    // Extra safety delay before proceeding with network changes
    vTaskDelay(pdMS_TO_TICKS(200));
#else
    ESP_LOGI(TAG, "===== CRITICAL: Socket reset for internal_mode =====");
    force_socket_cleanup();
    ESP_LOGI(TAG, "===== Socket reset for internal_mode completed =====");
#endif

    // Now it's safe to modify WiFi configuration
    esp_netif_ip_info_t ip_info;
    if (get_ap_ip_info(&ip_info) != ESP_OK) {
#ifndef USE_EXTERNAL_ADC
        atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Create and bind socket
    if (new_sock != -1) {
        safe_close(new_sock);
        new_sock = -1;
    }

    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
#ifndef USE_EXTERNAL_ADC
        atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info.ip.addr;
    new_addr.sin_port = htons(0); // Let the OS assign a port

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
#ifndef USE_EXTERNAL_ADC
        atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
#ifndef USE_EXTERNAL_ADC
        atomic_store(&wifi_operation_requested, 0); // Release lock before returning
#endif
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

#ifndef USE_EXTERNAL_ADC
    // Allow socket task to resume with clean ADC state
    atomic_store(&wifi_operation_requested, 0);
#endif

    // Get socket details
    char ip_str[16];
    struct sockaddr_in bound_addr;
    socklen_t addr_len = sizeof(bound_addr);
    if (getsockname(new_sock, (struct sockaddr *)&bound_addr, &addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &bound_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(bound_addr.sin_port);

    ESP_LOGI(TAG, "Socket created for internal mode - IP: %s, Port: %d", ip_str, new_port);
    return send_internal_mode_response(req, ip_str, new_port);
}
esp_err_t get_public_key_handler(httpd_req_t *req)
{
    // Configure CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // If it's an OPTIONS request, respond OK
    if (req->method == HTTP_OPTIONS) {
        return httpd_resp_send(req, NULL, 0);
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(response, "PublicKey", (char *)get_public_key());

    const char *json_response = cJSON_Print(response);
    if (json_response == NULL) {
        cJSON_Delete(response);
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(response);
    free((void *)json_response);

    return ret;
}

esp_err_t test_connect_handler(httpd_req_t *req)
{
    return httpd_resp_send(req, "1", 1);
}

esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port)
{
    cJSON *response = cJSON_CreateObject();
    ESP_LOGI(TAG, "IP: %s, Port: %d", ip_str, new_port);
    cJSON_AddStringToObject(response, "IP", ip_str);
    cJSON_AddNumberToObject(response, "Port", new_port);

    const char *json_response = cJSON_Print(response);
    if (json_response == NULL) {
        cJSON_Delete(response);
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(response);
    free((void *)json_response);
    return ret;
}

esp_err_t send_wifi_response(httpd_req_t *req, const char *ip, int port, bool success)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "IP", ip ? ip : "");
    cJSON_AddNumberToObject(response, "Port", port);
    cJSON_AddStringToObject(response, "Success", success ? "true" : "false");

    const char *json_response = cJSON_Print(response);
    if (json_response == NULL) {
        cJSON_Delete(response);
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(response);
    free((void *)json_response);
    return ret;
}

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 81;
    config.ctrl_port = 32767;
    config.stack_size = 4096 * 4;
    config.max_uri_handlers = 11;
    config.max_resp_headers = 8;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register handlers for different endpoints
        httpd_uri_t reset_socket_uri = {
            .uri = "/reset", .method = HTTP_GET, .handler = reset_socket_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &reset_socket_uri);

        httpd_uri_t trigger_uri = {
            .uri = "/trigger", .method = HTTP_POST, .handler = trigger_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &trigger_uri);

        httpd_uri_t test_connect_uri = {
            .uri = "/testConnect", .method = HTTP_GET, .handler = test_connect_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &test_connect_uri);

        httpd_uri_t get_public_key_uri = {
            .uri = "/get_public_key", .method = HTTP_GET, .handler = get_public_key_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &get_public_key_uri);

        httpd_uri_t scan_wifi_uri = {
            .uri = "/scan_wifi", .method = HTTP_GET, .handler = scan_wifi_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &scan_wifi_uri);

        httpd_uri_t config_uri = {.uri = "/config", .method = HTTP_GET, .handler = config_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &config_uri);

        httpd_uri_t connect_wifi_uri = {
            .uri = "/connect_wifi", .method = HTTP_POST, .handler = connect_wifi_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &connect_wifi_uri);

        httpd_uri_t internal_mode_uri = {
            .uri = "/internal_mode", .method = HTTP_GET, .handler = internal_mode_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &internal_mode_uri);

        httpd_uri_t single_uri = {.uri = "/single", .method = HTTP_GET, .handler = single_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &single_uri);

        httpd_uri_t normal_uri = {.uri = "/normal", .method = HTTP_GET, .handler = normal_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &normal_uri);

        httpd_uri_t freq_uri = {.uri = "/freq", .method = HTTP_POST, .handler = freq_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &freq_uri);
    }

    return server;
}

httpd_handle_t start_second_webserver(void)
{
    // Stop the existing server if it is already running
    if (second_server != NULL) {
        httpd_stop(second_server);
        second_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 80;
    config.max_uri_handlers = 10; // Increase from default 8
    config.max_resp_headers = 8; // Increase if needed
    config.lru_purge_enable = true; // Enable LRU mechanism
    config.stack_size = 4096 * 1.5;

    if (httpd_start(&second_server, &config) == ESP_OK) {
        httpd_uri_t reset_socket_uri = {
            .uri = "/reset", .method = HTTP_GET, .handler = reset_socket_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &reset_socket_uri);

        httpd_uri_t test_uri = {.uri = "/test", .method = HTTP_POST, .handler = test_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &test_uri);

        httpd_uri_t config_uri = {.uri = "/config", .method = HTTP_GET, .handler = config_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &config_uri);

        httpd_uri_t trigger_uri = {
            .uri = "/trigger", .method = HTTP_POST, .handler = trigger_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &trigger_uri);

        httpd_uri_t single_uri = {.uri = "/single", .method = HTTP_GET, .handler = single_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &single_uri);

        httpd_uri_t normal_uri = {.uri = "/normal", .method = HTTP_GET, .handler = normal_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &normal_uri);

        httpd_uri_t freq_uri = {.uri = "/freq", .method = HTTP_POST, .handler = freq_handler, .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &freq_uri);
    }

    return second_server;
}