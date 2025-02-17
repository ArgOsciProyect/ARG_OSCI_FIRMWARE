#include "include.h"

static const char *TAG = "ESP32_AP";
static int new_sock = -1;
static httpd_handle_t second_server = NULL;
static TaskHandle_t socket_task_handle = NULL;
static unsigned char public_key[KEYSIZE];
static unsigned char private_key[KEYSIZE];
static SemaphoreHandle_t key_gen_semaphore;
static heap_trace_record_t trace_record[HEAP_TRACE_ITEMS];
static uint64_t wait_time_us;

// Global variables declaration
static atomic_int mode = 0;
static atomic_int last_state = 0;
static atomic_int trigger_edge = 0;
static atomic_int current_state = 0;
#define GPIO_INPUT_PIN GPIO_NUM_19 // Reemplaza GPIO_NUM_4 con el pin que desees usar

// Helper functions for device configuration
static double get_sampling_frequency(void)
{
    #ifdef USE_EXTERNAL_ADC
    return 2500000;
    #else
    return 1650000; // Hardcoded for now, will be calculated later
    #endif
}

static int dividing_factor(void)
{
    #ifdef USE_EXTERNAL_ADC
    return 1;
    #else
    return 6;
    #endif
}

static int get_bits_per_packet(void)
{
    return 16; // Current packet size in bits
}

static int get_data_mask(void)
{   
    #ifdef USE_EXTERNAL_ADC
    return 0x1FF8; // Mask for data bits
    #else
    return 0x0FFF; // Mask for data bits
    #endif
}

static int get_channel_mask(void)
{   
    #ifdef USE_EXTERNAL_ADC
    return 0x0; // Mask for channel bits
    #else
    return 0xF000; // Mask for channel bits
    #endif
}

static int get_useful_bits(void)
{   
    #ifdef USE_EXTERNAL_ADC
    return 10; // ADC resolution configured
    #else
    return ADC_BITWIDTH; // ADC resolution configured
    #endif
}

//Descarta los primeros datos de la trama
static int get_discard_head(void)
{
    #ifdef USE_EXTERNAL_ADC
    return 0;
    #else
    return 0;
    #endif
}

static int get_discard_trailer(void)
{
    return 0; 
}

static int get_samples_per_packet(void)
{
    int total_samples;
    #ifdef USE_EXTERNAL_ADC
    total_samples = BUF_SIZE; // Number of samples per send call
    #else
    total_samples = BUF_SIZE; // Number of samples per send call
    #endif

    return total_samples - get_discard_head() - get_discard_trailer();
}



static esp_err_t config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Config handler called");
    httpd_resp_set_type(req, "application/json");

    cJSON *config = cJSON_CreateObject();
    if (config == NULL)
    {
        return httpd_resp_send_500(req);
    }

    cJSON_AddNumberToObject(config, "sampling_frequency", get_sampling_frequency());
    cJSON_AddNumberToObject(config, "bits_per_packet", get_bits_per_packet());
    cJSON_AddNumberToObject(config, "data_mask", get_data_mask());
    cJSON_AddNumberToObject(config, "channel_mask", get_channel_mask());
    cJSON_AddNumberToObject(config, "useful_bits", get_useful_bits());
    cJSON_AddNumberToObject(config, "samples_per_packet", get_samples_per_packet());
    cJSON_AddNumberToObject(config, "dividing_factor", dividing_factor());
    cJSON_AddNumberToObject(config, "discard_head", get_discard_head());       // New field
    cJSON_AddNumberToObject(config, "discard_trailer", get_discard_trailer()); // New field

    const char *response = cJSON_Print(config);
    esp_err_t ret = httpd_resp_send(req, response, strlen(response));

    free((void *)response);
    cJSON_Delete(config);

    return ret;
}
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

#ifdef USE_EXTERNAL_ADC
void spi_master_init()
{
    esp_err_t ret;
    int freq;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_MISO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configurar el bus SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2*BUF_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER | 
                SPICOMMON_BUSFLAG_MISO |
                SPICOMMON_BUSFLAG_IOMUX_PINS,
        .intr_flags = ESP_INTR_FLAG_IRAM
    };

    // Inicializar el bus SPI
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 3);
    ESP_ERROR_CHECK(ret);

    // Configurar el dispositivo SPI
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 48 * 1000 * 1000, // Velocidad del reloj SPI (10 MHz)
        .mode = 0,                          // Modo SPI 0
        .spics_io_num = PIN_NUM_CS,         // Pin CS
        .queue_size = 7,                    // Tamaño de la cola de transacciones
        .pre_cb = NULL,                     // Callback antes de cada transacción
        .post_cb = NULL,                     // Callback después de cada transacción
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
        .cs_ena_pretrans = 11,
        .input_delay_ns = 4
    };

    // Inicializar el dispositivo SPI

    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master initialized.");
    spi_device_get_actual_freq(spi, &freq);
    ESP_LOGI(TAG, "Actual frequency: %d", freq);

}

void init_mcpwm_trigger(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SYNC_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TRIGGER_PWM_FREQ_HZ * 32,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 32,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_gpio_sync_src_config_t gpio_sync_config = {
        .group_id = 0,
        .gpio_num = SYNC_GPIO,
        .flags.active_neg = 1,
        .flags.io_loop_back = 0,
        .flags.pull_down = 1,
        .flags.pull_up = 0,
    };
    
    mcpwm_sync_handle_t gpio_sync = NULL;
    ESP_ERROR_CHECK(mcpwm_new_gpio_sync_src(&gpio_sync_config, &gpio_sync));

    mcpwm_timer_sync_phase_config_t sync_phase = {
        .sync_src = gpio_sync,
        .count_value = 0,
        .direction = MCPWM_TIMER_DIRECTION_UP
    };
    ESP_ERROR_CHECK(mcpwm_timer_set_phase_on_sync(timer, &sync_phase));

    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = TRIGGER_PWM_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_HIGH)));

    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, (uint32_t)(26)));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(generator, -1, true));
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}
#endif

#ifndef USE_EXTERNAL_ADC
void start_adc_sampling()
{
    ESP_LOGI(TAG, "Starting ADC sampling");

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL,
        .bit_width = ADC_BITWIDTH};

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE * 2,
        .conv_frame_size = 128,
        .flags.flush_pool = false,
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_continuous_config_t continuous_config = {
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
        .sample_freq_hz = SAMPLE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1};

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &continuous_config));

    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

void stop_adc_sampling()
{
    ESP_LOGI(TAG, "Stopping ADC sampling");
    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}
#endif

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
        .divider = 80,
        .counter_dir = TIMER_COUNT_DOWN,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS, // Deshabilitar la alarma
        .auto_reload = TIMER_AUTORELOAD_DIS,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // Calcular el tiempo de espera en microsegundos
    double sampling_frequency = get_sampling_frequency();
    wait_time_us = (BUF_SIZE / sampling_frequency) * 1000000;

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);
}

void configure_gpio(void)
{
    // Alimentar el watchdog antes de configurar el GPIO
    // esp_task_wdt_reset();

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;           // No interrupciones
    io_conf.mode = GPIO_MODE_INPUT;                  // Configurar como entrada
    io_conf.pin_bit_mask = (1ULL << GPIO_INPUT_PIN); // Seleccionar el pin
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;     // Deshabilitar pull-down
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;        // Habilitar pull-up
    gpio_config(&io_conf);

    // Alimentar el watchdog después de configurar el GPIO
    // esp_task_wdt_reset();
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
    // Iniciar el temporizador
    timer_start(TIMER_GROUP_0, TIMER_0);

    // Esperar hasta que el temporizador alcance el valor de alarma (0)
    uint64_t timer_val;
    while (1)
    {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
        if (timer_val == 0)
        {
            break;
        }
    }

    // Pausar el temporizador
    timer_pause(TIMER_GROUP_0, TIMER_0);

    // Reiniciar el temporizador
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);
}

void socket_task(void *pvParameters)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char addr_str[128];
    
    uint32_t len;

    #ifdef USE_EXTERNAL_ADC
    uint16_t buffer[BUF_SIZE];
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;
    t.rxlength = BUF_SIZE * 16;
    t.rx_buffer = buffer;
    t.flags = 0;
    len = BUF_SIZE * 2;
    #else
    uint8_t buffer[BUF_SIZE];
    #endif

    // Calculate actual data to send
    #ifdef USE_EXTERNAL_ADC
    size_t sample_size = sizeof(uint16_t);
    #else
    size_t sample_size = sizeof(uint8_t);
    #endif

    void *send_buffer = buffer + (get_discard_head() * sample_size);
    size_t send_len = get_samples_per_packet() * sample_size;

    int flags = MSG_MORE;

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
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            close(new_sock);
            new_sock = -1;
            continue;
        }
        else
        {
            inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG, "Client connected: %s, Port: %d", addr_str, ntohs(client_addr.sin_port));
        }

        #ifndef USE_EXTERNAL_ADC
        start_adc_sampling();
        #endif


        while (1)
        {
            if (mode == 1)
            {
                #ifdef USE_EXTERNAL_ADC
                // bool edge_detected = 0;
                // esp_err_t ret = spi_device_polling_start(spi, &t, portMAX_DELAY);
                // if (ret != ESP_OK) {
                //     ESP_LOGE(TAG, "SPI transaction failed");
                // }
                // TickType_t xLastWakeTime = xTaskGetTickCount();
                // TickType_t xCurrentTime = xTaskGetTickCount();
                // while(pdMS_TO_TICKS(11) > (xCurrentTime - xLastWakeTime)){
                //     current_state = gpio_get_level(GPIO_INPUT_PIN);
                //     edge_detected = (trigger_edge == 1) ? (current_state > last_state) : // Positive edge
                //                             (current_state < last_state);                    // Negative edge

                //     last_state = current_state;
                //     xCurrentTime = xTaskGetTickCount();
                //     if (!edge_detected)
                //     {
                //         continue;
                //     }
                //     break;
                // }
                // ret = spi_device_polling_end(spi, &t);
                // if (ret != ESP_OK) {
                //     ESP_LOGE(TAG, "SPI transaction failed");
                // }
                // if (!edge_detected)
                // {
                //     continue;
                // }
                // if (ret == ESP_OK && len > 0)
                // {
                // // Prepare data for sending
                //     ssize_t sent = send(client_sock, send_buffer, send_len, flags);
                //     if (sent < 0)
                //     {
                //         if (errno == EAGAIN || errno == EWOULDBLOCK)
                //         {
                //             vTaskDelay(pdMS_TO_TICKS(10));
                //             continue;
                //         }
                //         ESP_LOGE(TAG, "Send error: errno %d", errno);
                //         break;
                //     }
                // }
                // continue;
                #else
                TickType_t xLastWakeTime = xTaskGetTickCount();
                current_state = gpio_get_level(GPIO_INPUT_PIN);

                //// Debug log moved before state update
                // ESP_LOGI(TAG, "Current: %d, Previous: %d, Edge type: %s",
                //          current_state, last_state,
                //          trigger_edge ? "positive" : "negative");

                // Check for specific edge type
                bool edge_detected = (trigger_edge == 1) ? (current_state > last_state) : // Positive edge
                                         (current_state < last_state);                    // Negative edge

                // TODO Ver si usar un valor diferente de delay para positivo o negativo
                // TODO Soluciona el problema de que uno aparece muy a la izquierda y el otro muy a la derecha
                last_state = current_state;
                
                if (!edge_detected)
                {
                    continue;
                }

                // If we get here, edge was detected

                TickType_t xCurrentTime = xTaskGetTickCount();
                vTaskDelay(pdMS_TO_TICKS(12) - (xCurrentTime - xLastWakeTime));
                // esp_rom_delay_us(24824);
                #endif
            }
            #ifdef USE_EXTERNAL_ADC
            esp_err_t ret = spi_device_polling_transmit(spi, &t);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "SPI transaction failed");
            }

            #else
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
            #endif

            if (ret == ESP_OK && len > 0)
            {

                // Prepare data for sending
                ssize_t sent = send(client_sock, send_buffer, send_len, flags);
                if (sent < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    ESP_LOGE(TAG, "Send error: errno %d", errno);
                    break;
                }
            }
            else
            {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10)
                {
                    ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
                    read_miss_count = 0;
                }
            }
        }

        #ifndef USE_EXTERNAL_ADC
        stop_adc_sampling();
        #endif

        safe_close(client_sock);
        ESP_LOGI(TAG, "Client disconnected");
    }
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

// Add to global variables
static ledc_channel_config_t ledc_channel;
#define TRIGGER_PWM_FREQ 78125 // 25kHz
#define TRIGGER_PWM_TIMER LEDC_TIMER_0
#define TRIGGER_PWM_CHANNEL LEDC_CHANNEL_0
#define TRIGGER_PWM_GPIO GPIO_NUM_26      // Choose appropriate GPIO
#define TRIGGER_PWM_RES LEDC_TIMER_10_BIT // 8-bit resolution (0-255)

void init_trigger_pwm(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = TRIGGER_PWM_RES,
        .freq_hz = TRIGGER_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = TRIGGER_PWM_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel.channel = TRIGGER_PWM_CHANNEL;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = TRIGGER_PWM_GPIO;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = TRIGGER_PWM_TIMER;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static esp_err_t set_trigger_level(int percentage)
{
    if (percentage < 0 || percentage > 100)
    {
        return ESP_FAIL;
    }

    // Convert percentage to duty cycle (0-255)
    uint32_t duty = (percentage * 1 << LEDC_TIMER_10_BIT) / 100;
    ESP_LOGI(TAG, "Setting PWM trigger to %d%% (duty: %lu)", percentage, duty);

    return ledc_set_duty(LEDC_LOW_SPEED_MODE, TRIGGER_PWM_CHANNEL, duty) == ESP_OK &&
                   ledc_update_duty(LEDC_LOW_SPEED_MODE, TRIGGER_PWM_CHANNEL) == ESP_OK
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t trigger_handler(httpd_req_t *req)
{
    char content[100];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0)
    {
        return httpd_resp_send_408(req);
    }
    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get trigger edge setting
    cJSON *edge = cJSON_GetObjectItem(root, "trigger_edge");
    if (cJSON_IsString(edge))
    {
        if (strcmp(edge->valuestring, "positive") == 0)
        {
            trigger_edge = 1;
        }
        else if (strcmp(edge->valuestring, "negative") == 0)
        {
            trigger_edge = 0;
        }
    }

    // Get trigger percentage
    cJSON *trigger = cJSON_GetObjectItem(root, "trigger_percentage");
    if (!cJSON_IsNumber(trigger))
    {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int percentage = (int)trigger->valuedouble;
    esp_err_t ret = set_trigger_level(percentage);

    cJSON_Delete(root);

    if (ret != ESP_OK)
    {
        return httpd_resp_send_500(req);
    }

    // Send success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "set_percentage", percentage);
    cJSON_AddStringToObject(response, "edge", trigger_edge ? "positive" : "negative");
    const char *json_response = cJSON_Print(response);
    httpd_resp_send(req, json_response, strlen(json_response));

    free((void *)json_response);
    cJSON_Delete(response);

    return ESP_OK;
}
static esp_err_t single_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Single handler called");

    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");

    mode = 1;
    last_state = gpio_get_level(GPIO_INPUT_PIN);

    // Simple JSON response
    const char *response = "{\"mode\":\"Single\"}";
    return httpd_resp_send(req, response, strlen(response));
}

static esp_err_t reset_socket_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reset socket handler called");
    
    // Get appropriate IP info based on request source port
    esp_netif_ip_info_t ip_info;
    if (httpd_req_get_hdr_value_len(req, "Host") > 0) {
        char host[32];
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        // Check if request came from AP server (port 81)
        bool is_ap_server = (strstr(host, ":81") != NULL);
        
        if (is_ap_server) {
            if (get_ap_ip_info(&ip_info) != ESP_OK) {
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        } else {
            if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) != ESP_OK) {
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        }
    } else {
        // Fallback to AP mode if can't determine source
        if (get_ap_ip_info(&ip_info) != ESP_OK) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
    }

    // Close existing socket if any
    if (new_sock != -1) {
        safe_close(new_sock);
        new_sock = -1;
    }

    // Create and bind new socket
    if (create_socket_and_bind(&ip_info) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get socket info
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

static esp_err_t normal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Normal handler called");

    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");

    mode = 0;

    // Simple JSON response
    const char *response = "{\"mode\":\"Normal\"}";
    return httpd_resp_send(req, response, strlen(response));
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
    config.max_uri_handlers = 9;    // Increase from default 8 to accommodate all handlers
    config.max_resp_headers = 8;    // Increase if needed
    config.lru_purge_enable = true; // Enable LRU mechanism
    config.stack_size = 4096 * 1.5;
    if (httpd_start(&second_server, &config) == ESP_OK)
    {

        httpd_uri_t reset_socket_uri = {
            .uri = "/reset_socket",
            .method = HTTP_GET,
            .handler = reset_socket_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &reset_socket_uri);
        httpd_uri_t test_uri = {
            .uri = "/test",
            .method = HTTP_POST,
            .handler = test_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &test_uri);

        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &config_uri);

        httpd_uri_t trigger_uri = {
            .uri = "/trigger",
            .method = HTTP_POST,
            .handler = trigger_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &trigger_uri);

        httpd_uri_t single_uri = {
            .uri = "/single",
            .method = HTTP_GET,
            .handler = single_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &single_uri);

        httpd_uri_t normal_uri = {
            .uri = "/normal",
            .method = HTTP_GET,
            .handler = normal_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(second_server, &normal_uri);
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



static esp_err_t send_wifi_response(httpd_req_t *req, const char *ip, int port, bool success) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "IP", ip ? ip : "");
    cJSON_AddNumberToObject(response, "Port", port);
    cJSON_AddStringToObject(response, "Success", success ? "true" : "false");
    
    const char *json_response = cJSON_Print(response);
    esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));
    
    cJSON_Delete(response);
    free((void *)json_response);
    return ret;
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
        return send_wifi_response(req, "", 0, false);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
        return send_wifi_response(req, "", 0, false);
    }

    esp_netif_ip_info_t ip_info;
    if (wait_for_ip(&ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP address");
        return send_wifi_response(req, "", 0, false);
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
        return send_wifi_response(req, "", 0, false);
    }

    char ip_str[16];
    struct sockaddr_in new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    if (getsockname(new_sock, (struct sockaddr *)&new_addr, &new_addr_len) != 0)
    {
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

static esp_err_t test_connect_handler(httpd_req_t *req)
{
    return httpd_resp_send(req, "1", 1);
}
void print_memory_stats(void)
{
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap: %lu bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Largest free block: %u bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Total allocated: %u bytes", info.total_allocated_bytes);
    ESP_LOGI(TAG, "Total free: %u bytes", info.total_free_bytes);
    ESP_LOGI(TAG, "Total blocks: %u", info.total_blocks);
    ESP_LOGI(TAG, "Free blocks: %u", info.free_blocks);
}
void memory_monitor_task(void *pvParameters)
{
    const TickType_t xDelay = pdMS_TO_TICKS(10000); // 10 seconds

    while(1) {
        print_memory_stats();
        vTaskDelay(xDelay);
    }
}


httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0; // Run on core 0
    config.server_port = 81;
    config.ctrl_port = 32767;
    config.stack_size = 4096 * 4;
    config.max_uri_handlers = 10;    // Increase from default 8 to accommodate all handlers
    config.max_resp_headers = 8;    // Increase if needed
    config.lru_purge_enable = true; // Enable LRU mechanism

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {

        httpd_uri_t reset_socket_uri = {
            .uri = "/reset_socket",
            .method = HTTP_GET,
            .handler = reset_socket_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &reset_socket_uri);

        httpd_uri_t trigger_uri = {
            .uri = "/trigger",
            .method = HTTP_POST,
            .handler = trigger_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &trigger_uri);

        httpd_uri_t test_connect_uri = {
            .uri = "/testConnect",
            .method = HTTP_GET,
            .handler = test_connect_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &test_connect_uri);

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

        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &config_uri);

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

        // Registrar los nuevos URI
        httpd_uri_t single_uri = {
            .uri = "/single",
            .method = HTTP_GET,
            .handler = single_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &single_uri);

        httpd_uri_t normal_uri = {
            .uri = "/normal",
            .method = HTTP_GET,
            .handler = normal_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &normal_uri);
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

    // ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

    xTaskCreate(dac_sine_wave_task, "dac_sine_wave_task", 2048, NULL, 5, NULL);
    
    init_trigger_pwm(); // Inicializar DAC para trigger

    #ifdef USE_EXTERNAL_ADC
    spi_master_init();
    init_mcpwm_trigger();
    #endif

    // Inicializar Wi-Fi
    wifi_init();

    // Iniciar servidor web
    start_webserver();

    // Iniciar el temporizador
    // my_timer_init();
    configure_gpio();
    

    // Crear la tarea para manejar el socket en el núcleo 1
    xTaskCreate(memory_monitor_task, "memory_monitor", 2048, NULL, 1, NULL);

    xTaskCreatePinnedToCore(socket_task, "socket_task", 55000, NULL, 5, &socket_task_handle, 1);
}