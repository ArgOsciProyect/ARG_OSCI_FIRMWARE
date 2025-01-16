#include "include.h"


static const char *TAG = "ESP32_AP";
static int new_sock = -1;
static httpd_handle_t second_server = NULL;
static TaskHandle_t socket_task_handle = NULL;
static unsigned char public_key[KEYSIZE];
static unsigned char private_key[KEYSIZE];
static SemaphoreHandle_t key_gen_semaphore;

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

void start_adc_sampling() {
    ESP_LOGI(TAG, "Starting ADC sampling");

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL,
        .bit_width = ADC_BITWIDTH_9
    };

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE * 2,
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

static void generate_key_pair_task(void *pvParameters) {
    ESP_LOGI(TAG, "Generating RSA key pair...");
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "gen_key_pair";

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers))) != 0) {
        goto exit;
    }
    else {
        ESP_LOGI(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
    }

    if ((ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        goto exit;
    }
    else {
        ESP_LOGI(TAG, "mbedtls_pk_setup returned %d", ret);
    }

    if ((ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg, KEYSIZE, 65537)) != 0) {
        ESP_LOGI(TAG, "mbedtls_rsa_gen_key returned %d", ret);
        goto exit;
    }
    else {
        ESP_LOGI(TAG, "mbedtls_rsa_gen_key returned %d", ret);
    }

    memset(public_key, 0, sizeof(public_key));
    if ((ret = mbedtls_pk_write_pubkey_pem(&pk, public_key, sizeof(public_key))) != 0) {
        goto exit;
    }

    memset(private_key, 0, sizeof(private_key));
    if ((ret = mbedtls_pk_write_key_pem(&pk, private_key, sizeof(private_key))) != 0) {
        goto exit;
    }
    else {
        //ESP_LOGI(TAG, "Private Key:\n%s", (char*)private_key);
    }

    //ESP_LOGI(TAG, "Public Key:\n%s", (char*)public_key);

exit:
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    // Dar el semáforo para indicar que la clave ha sido generada
    xSemaphoreGive(key_gen_semaphore);

    // Eliminar la tarea
    vTaskDelete(NULL);
}

void my_timer_init() {
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = TIMER_AUTORELOAD_EN,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_US * (TIMER_SCALE / 1000000));
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_start(TIMER_GROUP_0, TIMER_0);
}


void timer_wait() {
    uint64_t timer_val;
    timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
    while (timer_val < TIMER_INTERVAL_US * (TIMER_SCALE / 1000000)) {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
    }
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
}

void socket_task(void *pvParameters) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char addr_str[128];
    uint8_t buffer[BUF_SIZE];
    uint32_t len;

    while (1) {
        if (new_sock == -1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        int client_sock = accept(new_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            close(new_sock);
            new_sock = -1;
            continue;
        } else {
            inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG, "Client connected: %s, Port: %d", addr_str, ntohs(client_addr.sin_port));
        }

        start_adc_sampling();

        while (1) {
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            if (ret == ESP_OK && len > 0) {
                //for (int i = 0; i < len; i += sizeof(adc_digi_output_data_t)) {
                //    adc_digi_output_data_t *adc_data = (adc_digi_output_data_t *)&buffer[i];
                //    uint16_t adc_value = adc_data->type1.data;
                //    ESP_LOGI(TAG, "ADC Reading: %d", adc_value);
                //    vTaskDelay(1 / portTICK_PERIOD_MS);
                //}
                if (send(client_sock, buffer, len, 0) < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
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
        close(client_sock);
        ESP_LOGI(TAG, "Client disconnected");
    }
}
int decrypt_with_private_key(unsigned char *input, size_t input_len, unsigned char *output, size_t *output_len) {
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "decrypt";

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers))) != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        goto exit;
    }

    if ((ret = mbedtls_pk_parse_key(&pk, private_key, strlen((char *)private_key) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg)) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_parse_key returned %d", ret);
        goto exit;
    }

    size_t max_output_len = *output_len;  // Assume *output_len is the buffer size
    if ((ret = mbedtls_pk_decrypt(&pk, input, input_len, output, output_len, max_output_len, mbedtls_ctr_drbg_random, &ctr_drbg)) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_decrypt returned %d", ret);
        ESP_LOGE(TAG, "output_len: %d", *output_len);
        goto exit;
    }

exit:
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return ret;
}

static void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record) {
    bool ssid_exists = false;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        cJSON *ssid = cJSON_GetObjectItem(item, "SSID");
        if (ssid && strcmp(ssid->valuestring, (char *)ap_record->ssid) == 0) {
            ssid_exists = true;
            break;
        }
    }
    if (!ssid_exists && strlen((char *)ap_record->ssid) > 0) {
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "SSID", (char *)ap_record->ssid);
        cJSON_AddItemToArray(root, item);
    }
}

static cJSON* scan_and_get_ap_records(uint16_t *num_networks) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(num_networks));
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * (*num_networks));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(num_networks, ap_records));
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < *num_networks; i++) {
        add_unique_ssid(root, &ap_records[i]);
    }
    free(ap_records);
    return root;
}

esp_err_t scan_wifi_handler(httpd_req_t *req) {
    uint16_t num_networks = 0;
    cJSON *root = scan_and_get_ap_records(&num_networks);
    const char *json_response = cJSON_Print(root);
    httpd_resp_send(req, json_response, strlen(json_response));
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
    config.server_port = 80;
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
static esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config) {
    char content[KEYSIZE]; // Aumentar el tamaño del buffer para acomodar datos encriptados
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
    ESP_LOGI(TAG, "SSID: %s", ssid_encrypted->valuestring);
    ESP_LOGI(TAG, "Password: %s", password_encrypted->valuestring);

    // Decodificar SSID en base64
    unsigned char ssid_decoded[512];
    size_t ssid_decoded_len;
    if ((ret = mbedtls_base64_decode(ssid_decoded, sizeof(ssid_decoded), &ssid_decoded_len, (unsigned char *)ssid_encrypted->valuestring, strlen(ssid_encrypted->valuestring))) != 0) {
        ESP_LOGE(TAG, "mbedtls_base64_decode returned %d", ret);
        ESP_LOGE(TAG, "ssid_decoded_len: %d", ssid_decoded_len);
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Decodificar Password en base64
    unsigned char password_decoded[512];
    size_t password_decoded_len;
    if ((ret = mbedtls_base64_decode(password_decoded, sizeof(password_decoded), &password_decoded_len, (unsigned char *)password_encrypted->valuestring, strlen(password_encrypted->valuestring))) != 0) {
        ESP_LOGE(TAG, "mbedtls_base64_decode returned %d", ret);
        ESP_LOGE(TAG, "password_decoded_len: %d", password_decoded_len);
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Desencriptar SSID
    unsigned char ssid_decrypted[512];
    size_t ssid_decrypted_len = sizeof(ssid_decrypted);
    if (decrypt_with_private_key(ssid_decoded, ssid_decoded_len, ssid_decrypted, &ssid_decrypted_len) != 0) {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Desencriptar Password
    unsigned char password_decrypted[512];
    size_t password_decrypted_len = sizeof(password_decrypted);
    if (decrypt_with_private_key(password_decoded, password_decoded_len, password_decrypted, &password_decrypted_len) != 0) {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy((char *)wifi_config->sta.ssid, (char *)ssid_decrypted, sizeof(wifi_config->sta.ssid));
    strncpy((char *)wifi_config->sta.password, (char *)password_decrypted, sizeof(wifi_config->sta.password));
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info) {
    for (int i = 0; i < 10; i++) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ip_info) == ESP_OK && ip_info->ip.addr != 0) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info) {
    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info->ip.addr;
    new_addr.sin_port = htons(0);

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t connect_wifi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Connecting to Wi-Fi network");
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = ""
        },
    };

    if (parse_wifi_credentials(req, &wifi_config) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    if (wait_for_ip(&ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (new_sock != -1) {
        close(new_sock);
        new_sock = -1;
    }
    if (second_server != NULL) {
        httpd_stop(second_server);
        second_server = NULL;
    }

    if (create_socket_and_bind(&ip_info) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    // Obtener la BSSID del AP al que está conectado el ESP32
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP info");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "IP", ip_str);
    cJSON_AddNumberToObject(response, "Port", new_port);
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
             ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
    cJSON_AddStringToObject(response, "BSSID", bssid_str);
    const char *json_response = cJSON_Print(response);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(response);
    free((void *)json_response);

    second_server = start_second_webserver();

    return ESP_OK;
}

static esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info) {
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), ip_info) != ESP_OK || ip_info->ip.addr == 0) {
        ESP_LOGE(TAG, "Failed to get IP address of AP");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t create_and_bind_socket(esp_netif_ip_info_t *ip_info) {
    if (new_sock != -1) {
        close(new_sock);
        new_sock = -1;
    }

    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info->ip.addr;
    new_addr.sin_port = htons(0);

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0) {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
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

static esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port) {
    cJSON *response = cJSON_CreateObject();
    ESP_LOGI(TAG, "IP: %s, Port: %d", ip_str, new_port);
    cJSON_AddStringToObject(response, "IP", ip_str);
    cJSON_AddNumberToObject(response, "Port", new_port);
    const char *json_response = cJSON_Print(response);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(response);
    free((void *)json_response);
    return ESP_OK;
}

esp_err_t internal_mode_handler(httpd_req_t *req) {
    esp_netif_ip_info_t ip_info;
    if (get_ap_ip_info(&ip_info) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (create_and_bind_socket(&ip_info) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    memset(ip_str, 0, sizeof(ip_str));
    int new_port;

    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0) {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    new_port = ntohs(new_addr.sin_port);
    ESP_LOGI(TAG, "IP: %s, Port: %d", ip_str, new_port);
    return send_internal_mode_response(req, ip_str, new_port);
}

esp_err_t get_public_key_handler(httpd_req_t *req) {

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    // Si es una solicitud OPTIONS, responder OK
    if (req->method == HTTP_OPTIONS) {
        return httpd_resp_send(req, NULL, 0);
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(response, "PublicKey", (char *)public_key);

    const char *json_response = cJSON_Print(response);
    if (json_response == NULL) {
        cJSON_Delete(response);
        return httpd_resp_send_500(req);
    }

    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(response);
    free((void *)json_response);

    return ret;
}

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 81;
    config.ctrl_port = 32767;
    config.stack_size = 4096*4;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t get_public_key_uri = {
            .uri = "/get_public_key",
            .method = HTTP_GET,
            .handler = get_public_key_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &get_public_key_uri);

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

    // Crear el semáforo
    key_gen_semaphore = xSemaphoreCreateBinary();
    if (key_gen_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    esp_task_wdt_deinit();
    // Crear la tarea para generar la clave
    xTaskCreate(generate_key_pair_task, "generate_key_pair_task", 8192, NULL, 5, NULL);

    // Esperar a que se genere la clave
    if (xSemaphoreTake(key_gen_semaphore, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take semaphore");
        return;
    }

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 1000000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Bitmask of all cores
        .trigger_panic = false,
    };

    ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

    xTaskCreate(dac_sine_wave_task, "dac_sine_wave_task", 2048, NULL, 5, NULL);

    // Inicializar Wi-Fi
    wifi_init();

    // Iniciar servidor web
    start_webserver();

    // Crear la tarea para manejar el socket en el núcleo 1
    xTaskCreatePinnedToCore(socket_task, "socket_task", 50000, NULL, 5, &socket_task_handle, 1);
}