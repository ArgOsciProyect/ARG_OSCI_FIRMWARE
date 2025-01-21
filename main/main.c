#include "include.h"

static const char *TAG = "ESP32_AP";
static TaskHandle_t socket_task_handle = NULL;
static httpd_handle_t second_server = NULL;
static SemaphoreHandle_t key_gen_semaphore;
static unsigned char public_key[KEYSIZE];
static unsigned char private_key[KEYSIZE];
static int new_sock = -1;
static spi_device_handle_t spi_handle = NULL;

#ifdef CONFIG_HEAP_TRACING
static heap_trace_record_t trace_record[HEAP_TRACE_ITEMS];
#endif

static esp_err_t safe_close(int sock)
{
    if (sock < 0)
    {
        return ESP_OK; // Already closed
    }

    ESP_LOGI(TAG, "Attempting to safely close socket %d", sock);
    bool force_close = false;
    esp_err_t ret = ESP_OK;

    // Try graceful shutdown first
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 30; // 30 second timeout

    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set SO_LINGER on socket %d", sock);
        force_close = true;
    }

    // Shutdown both sending and receiving if not forcing close
    if (!force_close && shutdown(sock, SHUT_RDWR) < 0)
    {
        ESP_LOGW(TAG, "Shutdown failed for socket %d, errno %d", sock, errno);
        force_close = true;
    }

    if (force_close)
    {
        // Set immediate close
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;

        if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0)
        {
            ESP_LOGE(TAG, "Failed to set immediate close on socket %d", sock);
        }
    }

    // Close the socket
    if (close(sock) < 0)
    {
        ESP_LOGE(TAG, "Close failed for socket %d, errno %d", sock, errno);
        ret = ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "Successfully closed socket %d", sock);
    }

    return ret;
}

#ifdef DEBUG_SPI_SIGNAL
#define FAKE_DATA_FREQ 1000 // 1kHz
static uint8_t fake_data_buffer[BUF_SIZE];
static spi_device_handle_t spi_slave_handle = NULL;
static TaskHandle_t spi_fake_task_handle = NULL;

// Generar datos sintéticos (ej: onda senoidal)
static void generate_fake_data()
{
    static float phase = 0;
    for (int i = 0; i < BUF_SIZE; i++)
    {
        fake_data_buffer[i] = (uint8_t)(127 + 127 * sin(phase));
        phase += 2 * 3.14159 * FAKE_DATA_FREQ / BUF_SIZE;
        if (phase >= 2 * 3.14159)
            phase -= 2 * 3.14159;
    }
}

static void init_spi_slave()
{
    spi_bus_config_t buscfg = {
        .miso_io_num = 13,
        .mosi_io_num = 15,
        .sclk_io_num = 14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUF_SIZE};

    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = 21,
        .queue_size = 3,
        .flags = 0,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI3_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));
}

static void fake_data_task(void *pvParameters)
{
    spi_slave_transaction_t t;

    // Initialize SPI slave first
    init_spi_slave();

    while (1)
    {
        generate_fake_data();

        memset(&t, 0, sizeof(t));
        t.length = BUF_SIZE * 8;
        t.tx_buffer = fake_data_buffer;
        t.rx_buffer = NULL;

        ESP_ERROR_CHECK(spi_slave_transmit(SPI3_HOST, &t, portMAX_DELAY));
    }
}
#endif

#ifdef CONFIG_HEAP_TRACING
void init_heap_trace(void)
{
    ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, HEAP_TRACE_ITEMS));
}

void test_memory_leaks(void)
{
    // Iniciar rastreo
    ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));

    // Ejecutar la función a probar
    httpd_req_t req; // Mock request
    test_handler(&req);

    // Detener rastreo
    ESP_ERROR_CHECK(heap_trace_stop());

    // Imprimir resultados
    heap_trace_dump();
}
#endif

void wifi_init()
{
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL)
    {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL)
    {
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
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    if (strlen(WIFI_PASSWORD) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void generate_key_pair_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Generating RSA key pair...");
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "gen_key_pair";

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers))) != 0)
    {
        goto exit;
    }
    else
    {
        ESP_LOGI(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
    }

    if ((ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0)
    {
        goto exit;
    }
    else
    {
        ESP_LOGI(TAG, "mbedtls_pk_setup returned %d", ret);
    }

    if ((ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg, KEYSIZE, 65537)) != 0)
    {
        ESP_LOGI(TAG, "mbedtls_rsa_gen_key returned %d", ret);
        goto exit;
    }
    else
    {
        ESP_LOGI(TAG, "mbedtls_rsa_gen_key returned %d", ret);
    }

    memset(public_key, 0, sizeof(public_key));
    if ((ret = mbedtls_pk_write_pubkey_pem(&pk, public_key, sizeof(public_key))) != 0)
    {
        goto exit;
    }

    memset(private_key, 0, sizeof(private_key));
    if ((ret = mbedtls_pk_write_key_pem(&pk, private_key, sizeof(private_key))) != 0)
    {
        goto exit;
    }
    else
    {
        // ESP_LOGI(TAG, "Private Key:\n%s", (char*)private_key);
    }

    // ESP_LOGI(TAG, "Public Key:\n%s", (char*)public_key);

exit:
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    // Dar el semáforo para indicar que la clave ha sido generada
    xSemaphoreGive(key_gen_semaphore);

    // Eliminar la tarea
    vTaskDelete(NULL);
}

void my_timer_init()
{
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

static esp_err_t decrypt_base64_message(const char *encrypted_base64, char *decrypted_output, size_t output_size)
{
    // Base64 decode
    unsigned char decoded[512];
    size_t decoded_len;
    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                                    (unsigned char *)encrypted_base64,
                                    strlen(encrypted_base64));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        return ESP_FAIL;
    }

    // Decrypt
    size_t decrypted_len = output_size;
    ret = decrypt_with_private_key(decoded, decoded_len,
                                   (unsigned char *)decrypted_output,
                                   &decrypted_len);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Decryption failed: %d", ret);
        return ESP_FAIL;
    }

    // Ensure null termination
    decrypted_output[decrypted_len] = '\0';
    return ESP_OK;
}

void timer_wait()
{
    uint64_t timer_val;
    timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
    while (timer_val < TIMER_INTERVAL_US * (TIMER_SCALE / 1000000))
    {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
    }
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
}

static void spi_init_bus_and_device(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = 19,
        .mosi_io_num = 23,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUF_SIZE};
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = 5,
        .queue_size = 3};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));
}

static esp_err_t read_from_spi(uint8_t *buffer, size_t *out_len)
{
    uint8_t dummy_tx[1] = {0}; // Sin datos reales a transmitir
    spi_transaction_t t = {
        .length = BUF_SIZE * 8,
        .rxlength = BUF_SIZE * 8,
        .tx_buffer = dummy_tx,
        .rx_buffer = buffer};
    esp_err_t ret = spi_device_transmit(spi_handle, &t);
    if (ret == ESP_OK)
    {
        *out_len = BUF_SIZE;
    }
    return ret;
}

void stop_spi()
{
    if (spi_handle)
    {
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
    }
    spi_bus_free(SPI2_HOST);
}

static bool is_valid_spi_data(const uint8_t *buffer, size_t len) {
    if (len < 3) { // Mínimo: header + 1 dato + footer
        return false;
    }
    
    // Verificar header y footer
    if (buffer[0] != SPI_VALID_HEADER || buffer[len-1] != SPI_VALID_FOOTER) {
        return false;
    }
    
    // Verificar que los datos estén en rango válido
    for (size_t i = 1; i < len-1; i++) {
        if (buffer[i] < MIN_VALID_VALUE || buffer[i] > MAX_VALID_VALUE) {
            return false;
        }
    }
    
    return true;
}

static void socket_task(void *pvParameters)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char addr_str[128];
    uint8_t buffer[BUF_SIZE];
    size_t len;

    spi_init_bus_and_device();

    while (1)
    {
        if (new_sock == -1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        int client_sock = accept(new_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0)
        {
            close(new_sock);
            new_sock = -1;
            continue;
        }
        inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        while (1)
        {
            if (read_from_spi(buffer, &len) == ESP_OK && len > 0)
            {
                ssize_t sent = send(client_sock, buffer, len, MSG_MORE);
                if (sent < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    break;
                }
            }
            else
            {
                ESP_LOGW(TAG, "SPI read error");
            }
        }
        safe_close(client_sock);
    }
    stop_spi();
}

int decrypt_with_private_key(unsigned char *input, size_t input_len, unsigned char *output, size_t *output_len)
{
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "decrypt";

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers))) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        goto exit;
    }

    if ((ret = mbedtls_pk_parse_key(&pk, private_key, strlen((char *)private_key) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_pk_parse_key returned %d", ret);
        goto exit;
    }

    size_t max_output_len = *output_len; // Assume *output_len is the buffer size
    if ((ret = mbedtls_pk_decrypt(&pk, input, input_len, output, output_len, max_output_len, mbedtls_ctr_drbg_random, &ctr_drbg)) != 0)
    {
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

static void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record)
{
    bool ssid_exists = false;
    cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        cJSON *ssid = cJSON_GetObjectItem(item, "SSID");
        if (ssid && strcmp(ssid->valuestring, (char *)ap_record->ssid) == 0)
        {
            ssid_exists = true;
            break;
        }
    }
    if (!ssid_exists && strlen((char *)ap_record->ssid) > 0)
    {
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "SSID", (char *)ap_record->ssid);
        cJSON_AddItemToArray(root, item);
    }
}

static cJSON *scan_and_get_ap_records(uint16_t *num_networks)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true};
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(num_networks));
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * (*num_networks));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(num_networks, ap_records));
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < *num_networks; i++)
    {
        add_unique_ssid(root, &ap_records[i]);
    }
    free(ap_records);
    return root;
}

esp_err_t scan_wifi_handler(httpd_req_t *req)
{
    uint16_t num_networks = 0;
    cJSON *root = scan_and_get_ap_records(&num_networks);
    const char *json_response = cJSON_Print(root);
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_Delete(root);
    free((void *)json_response);
    return ESP_OK;
}

esp_err_t test_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Test handler called");

    // Read POST data
    char content[600];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0)
    {
        httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[received] = '\0';
    ESP_LOGI(TAG, "Received content: %s", content);

    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (!root)
    {
        ESP_LOGI(TAG, "Failed to parse JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get encrypted message and copy it
    cJSON *encrypted_msg = cJSON_GetObjectItem(root, "word");
    if (!encrypted_msg || !cJSON_IsString(encrypted_msg))
    {
        ESP_LOGI(TAG, "Failed to get encrypted message");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Copy the encrypted message since we'll free root
    char *encrypted_copy = strdup(encrypted_msg->valuestring);
    if (!encrypted_copy)
    {
        ESP_LOGI(TAG, "Failed to copy encrypted message");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Free root as we don't need it anymore
    cJSON_Delete(root);

    // Decrypt message
    char decrypted[256];
    if (decrypt_base64_message(encrypted_copy, decrypted, sizeof(decrypted)) != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to decrypt message");
        free(encrypted_copy);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Free encrypted copy as we don't need it anymore
    free(encrypted_copy);

    // Create response
    cJSON *response = cJSON_CreateObject();
    if (!response)
    {
        ESP_LOGI(TAG, "Failed to create response object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (!cJSON_AddStringToObject(response, "decrypted", decrypted))
    {
        ESP_LOGI(TAG, "Failed to add string to response");
        cJSON_Delete(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *json_response = cJSON_Print(response);
    if (!json_response)
    {
        ESP_LOGI(TAG, "Failed to print response");
        cJSON_Delete(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send response
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    // Cleanup in reverse order of allocation
    free((void *)json_response);
    cJSON_Delete(response);

    return ret;
}

httpd_handle_t start_second_webserver(void)
{
    // Detener el servidor existente si ya está en ejecución
    if (second_server != NULL)
    {
        httpd_stop(second_server);
        second_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 80;
    config.stack_size = 4096 * 1.5;
    if (httpd_start(&second_server, &config) == ESP_OK)
    {
        httpd_uri_t test_uri = {
            .uri = "/test",
            .method = HTTP_POST,
            .handler = test_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &test_uri);
    }

    return second_server;
}

static esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config)
{
    char content[KEYSIZE];
    int ret = httpd_req_recv(req, content, sizeof(content));
    ESP_LOGI(TAG, "Received content: %s", content);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(content);
    if (root == NULL)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *ssid_encrypted = cJSON_GetObjectItem(root, "SSID");
    cJSON *password_encrypted = cJSON_GetObjectItem(root, "Password");
    if (!cJSON_IsString(ssid_encrypted) || !cJSON_IsString(password_encrypted))
    {
        httpd_resp_send_408(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Desencriptar SSID
    char ssid_decrypted[512];
    if (decrypt_base64_message(ssid_encrypted->valuestring, ssid_decrypted, sizeof(ssid_decrypted)) != ESP_OK)
    {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Desencriptar Password
    char password_decrypted[512];
    if (decrypt_base64_message(password_encrypted->valuestring, password_decrypted, sizeof(password_decrypted)) != ESP_OK)
    {
        httpd_resp_send_500(req);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy((char *)wifi_config->sta.ssid, ssid_decrypted, sizeof(wifi_config->sta.ssid));
    strncpy((char *)wifi_config->sta.password, password_decrypted, sizeof(wifi_config->sta.password));
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info)
{
    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ip_info) == ESP_OK && ip_info->ip.addr != 0)
        {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info)
{
    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info->ip.addr;
    new_addr.sin_port = htons(0);

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0)
    {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t connect_wifi_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Connecting to Wi-Fi network");
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = ""},
    };

    if (parse_wifi_credentials(req, &wifi_config) != ESP_OK)
    {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    if (wait_for_ip(&ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP address");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (new_sock != -1)
    {
        safe_close(new_sock);
        new_sock = -1;
    }
    if (second_server != NULL)
    {
        httpd_stop(second_server);
        second_server = NULL;
    }

    if (create_socket_and_bind(&ip_info) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0)
    {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    int new_port = ntohs(new_addr.sin_port);

    // Obtener la BSSID del AP al que está conectado el ESP32
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
    {
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

static esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), ip_info) != ESP_OK || ip_info->ip.addr == 0)
    {
        ESP_LOGE(TAG, "Failed to get IP address of AP");
        return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t create_and_bind_socket(esp_netif_ip_info_t *ip_info)
{
    if (new_sock != -1)
    {
        safe_close(new_sock);
        new_sock = -1;
    }

    new_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (new_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_addr.s_addr = ip_info->ip.addr;
    new_addr.sin_port = htons(0);

    if (bind(new_sock, (struct sockaddr *)&new_addr, sizeof(new_addr)) != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    if (listen(new_sock, 1) != 0)
    {
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        return ESP_FAIL;
    }

    // Convert IP to string and get port number
    char ip_str[16];
    inet_ntop(AF_INET, &(new_addr.sin_addr), ip_str, sizeof(ip_str));
    int port = ntohs(new_addr.sin_port);

    ESP_LOGI(TAG, "Socket created and bound, IP: %s, PORT: %d", ip_str, port);
    return ESP_OK;
}
void init_sine_wave()
{
    dac_cosine_handle_t chan0_handle;
    dac_cosine_config_t cos0_cfg = {
        .chan_id = DAC_CHAN_0,
        .freq_hz = 10000, // Frecuencia de la señal senoidal en Hz
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos0_cfg, &chan0_handle));
    ESP_ERROR_CHECK(dac_cosine_start(chan0_handle));
}

void dac_sine_wave_task(void *pvParameters)
{
    init_sine_wave();
    vTaskDelete(NULL); // Finalizamos la tarea una vez que la señal está configurada
}

static esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port)
{
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

esp_err_t internal_mode_handler(httpd_req_t *req)
{
    esp_netif_ip_info_t ip_info;
    if (get_ap_ip_info(&ip_info) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (create_and_bind_socket(&ip_info) != ESP_OK)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char ip_str[16];
    memset(ip_str, 0, sizeof(ip_str));
    int new_port;

    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0)
    {
        ESP_LOGE(TAG, "Unable to get socket name: errno %d", errno);
        safe_close(new_sock);
        new_sock = -1;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    inet_ntop(AF_INET, &new_addr.sin_addr, ip_str, sizeof(ip_str));
    new_port = ntohs(new_addr.sin_port);
    ESP_LOGI(TAG, "IP: %s, Port: %d", ip_str, new_port);
    return send_internal_mode_response(req, ip_str, new_port);
}

esp_err_t get_public_key_handler(httpd_req_t *req)
{

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Si es una solicitud OPTIONS, responder OK
    if (req->method == HTTP_OPTIONS)
    {
        return httpd_resp_send(req, NULL, 0);
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL)
    {
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(response, "PublicKey", (char *)public_key);

    const char *json_response = cJSON_Print(response);
    if (json_response == NULL)
    {
        cJSON_Delete(response);
        return httpd_resp_send_500(req);
    }

    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));

    cJSON_Delete(response);
    free((void *)json_response);

    return ret;
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 81;
    config.ctrl_port = 32767;
    config.stack_size = 4096 * 4;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {

        httpd_uri_t get_public_key_uri = {
            .uri = "/get_public_key",
            .method = HTTP_GET,
            .handler = get_public_key_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &get_public_key_uri);

        httpd_uri_t scan_wifi_uri = {
            .uri = "/scan_wifi",
            .method = HTTP_GET,
            .handler = scan_wifi_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &scan_wifi_uri);

        httpd_uri_t connect_wifi_uri = {
            .uri = "/connect_wifi",
            .method = HTTP_POST,
            .handler = connect_wifi_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &connect_wifi_uri);

        httpd_uri_t internal_mode_uri = {
            .uri = "/internal_mode",
            .method = HTTP_GET,
            .handler = internal_mode_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &internal_mode_uri);
    }

    return server;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Crear el semáforo
    key_gen_semaphore = xSemaphoreCreateBinary();
    if (key_gen_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    esp_task_wdt_deinit();
    // Crear la tarea para generar la clave
    xTaskCreate(generate_key_pair_task, "generate_key_pair_task", 8192, NULL, 5, NULL);

    // Esperar a que se genere la clave
    if (xSemaphoreTake(key_gen_semaphore, portMAX_DELAY) != pdTRUE)
    {
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
#ifdef DEBUG_SPI_SIGNAL
    // Create fake data task on core 0
    xTaskCreatePinnedToCore(fake_data_task, "fake_data_task", 4096, NULL, 5, &spi_fake_task_handle, 0);
#endif
}