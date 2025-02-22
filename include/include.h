#ifndef INCLUDE_H
#define INCLUDE_H

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
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <stdatomic.h>
#include <freertos/task.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/base64.h>
#include <esp_task_wdt.h>
#include <driver/timer.h>
#include <driver/ledc.h>
#include "driver/dac_cosine.h"
#include "esp_adc/adc_continuous.h"
#include "driver/adc.h"
#include "driver/spi_master.h"
#include "driver/mcpwm_prelude.h"
#include "driver/pulse_cnt.h"


#define WIFI_SSID "ESP32_AP"
#define WIFI_PASSWORD "password123"
#define MAX_STA_CONN 4
#define PORT 8080
#define KEYSIZE 3072
#define KEYSIZEBITS 3072*8
#define TIMER_DIVIDER 16 //  Hardware timer clock divider
#define TIMER_BASE_CLK 80000000 // 80 MHz
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // Convert counter value to seconds
#define TIMER_INTERVAL_US 2048 // Timer interval in microseconds
#define MAX_CLIENTS 100
#define ADC_CHANNEL ADC_CHANNEL_5
#define ADC_BITWIDTH ADC_WIDTH_BIT_10
#define SAMPLE_RATE_HZ 2000000 // 2 MHz
#define GPIO_INPUT_PIN GPIO_NUM_11  // Using GPIO 11
#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   15
#define MCPWM_FREQ_HZ 2500000  // 2.5MHz
#define MCPWM_GPIO 16
#define SYNC_GPIO 17  // Pin for synchronization
#define CS_CLK_TO_PWM 10 // cs_ena_pretrans
#define DELAY_NS 33 // input_delay_ns
#define SPI_FREQ 40000000 // frecuencia_SPI
#define PERIOD_TICKS 32 // period_ticks MCPWM
#define COMPARE_VALUE 26 // compare_value MCPWM
#define TRIGGER_PWM_FREQ 78125 // 25kHz
#define TRIGGER_PWM_TIMER LEDC_TIMER_0
#define TRIGGER_PWM_CHANNEL LEDC_CHANNEL_0
#define TRIGGER_PWM_GPIO GPIO_NUM_26      // Choose appropriate GPIO
#define TRIGGER_PWM_RES LEDC_TIMER_10_BIT // 8-bit resolution (0-255)
#define SINGLE_INPUT_PIN GPIO_NUM_19
#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_HIGH_LIMIT INT16_MAX
#define PCNT_LOW_LIMIT INT16_MIN
#define MATRIX_SPI_ROWS 7
#define MATRIX_SPI_COLS 5
#define MATRIX_SPI_FREQ { \
    {40000000, CS_CLK_TO_PWM, DELAY_NS, PERIOD_TICKS, COMPARE_VALUE}, \
    {20000000, CS_CLK_TO_PWM-2, DELAY_NS+13, PERIOD_TICKS*2, COMPARE_VALUE*2}, \
    {10000000, CS_CLK_TO_PWM-3, DELAY_NS+38, PERIOD_TICKS*4, COMPARE_VALUE*4}, \
    {5000000, CS_CLK_TO_PWM-3, DELAY_NS+188, PERIOD_TICKS*8, COMPARE_VALUE*8}, \
    {2500000, CS_CLK_TO_PWM-3, DELAY_NS+88, PERIOD_TICKS*16, COMPARE_VALUE*16}, \
    {1250000, CS_CLK_TO_PWM-3, DELAY_NS+288, PERIOD_TICKS*32, COMPARE_VALUE*32}, \
    {625000, CS_CLK_TO_PWM-3, DELAY_NS+788, PERIOD_TICKS*64, COMPARE_VALUE*64}  \
}
#define SPI_FREQ_SCALE_FACTOR 1000/16

//#define USE_EXTERNAL_ADC  // Comment this line to use internal ADC

#ifdef USE_EXTERNAL_ADC
#define BUF_SIZE 17280*4
#else
#define BUF_SIZE 17280*3
#endif

static adc_continuous_handle_t adc_handle;
static atomic_int adc_modify_freq = 0;
static atomic_int adc_divider = 1;
static int read_miss_count = 0;
static spi_device_handle_t spi;
static mcpwm_timer_handle_t timer = NULL;
static mcpwm_oper_handle_t oper = NULL;
static mcpwm_cmpr_handle_t comparator = NULL;
static mcpwm_gen_handle_t generator = NULL;
static const uint32_t spi_matrix[MATRIX_SPI_ROWS][MATRIX_SPI_COLS] = MATRIX_SPI_FREQ;
static int spi_index = 0;
static ledc_channel_config_t ledc_channel;
static atomic_int mode = 0;
static atomic_int last_state = 0;
static atomic_int trigger_edge = 0;
static atomic_int current_state = 0;
static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
// Proteger acceso al SPI con semáforo
#ifdef USE_EXTERNAL_ADC
static SemaphoreHandle_t spi_mutex = NULL;
#endif

#ifdef CONFIG_HEAP_TRACING
    #include "esp_heap_trace.h"
    #define HEAP_TRACE_ITEMS 100
    void init_heap_trace(void);
    void test_memory_leaks(void);
#endif


/**
 * @brief Initialize Wi-Fi in APSTA mode.
 */
void wifi_init(void);

/**
 * @brief Initialize the hardware timer with specific configuration.
 *
 * This function sets up the timer with the following configuration:
 * - Divider: TIMER_DIVIDER
 * - Counter direction: Up
 * - Counter enable: Paused initially
 * - Alarm enable: Enabled
 * - Auto-reload: Enabled
 *
 * The timer is initialized for TIMER_GROUP_0 and TIMER_0. The counter value is set to 0,
 * and the alarm value is set based on TIMER_INTERVAL_US and TIMER_SCALE. The timer interrupt
 * is enabled, and the timer is started.
 */
void my_timer_init();

/**
 * @brief Waits for a specified timer interval to elapse.
 *
 * This function continuously checks the current value of the timer counter
 * until it reaches the specified interval. Once the interval has elapsed,
 * the timer counter is reset to zero.
 *
 * @note The timer interval is defined by the macro TIMER_INTERVAL_US and
 *       the timer scale is defined by the macro TIMER_SCALE.
 *
 * @param None
 *
 * @return None
 */
void timer_wait();

/**
 * @brief Task to handle socket communication.
 * 
 * @param pvParameters Parameters for the task.
 */
void socket_task(void *pvParameters);

/**
 * @brief HTTP handler to scan for available Wi-Fi networks.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t scan_wifi_handler(httpd_req_t *req);

/**
 * @brief Add unique SSID to the JSON array.
 * 
 * @param root JSON array.
 * @param ap_record Access point record.
 */
static void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record);

/**
 * @brief Scan and get access point records.
 * 
 * @param num_networks Pointer to store the number of networks found.
 * @return cJSON* JSON array of access point records.
 */
static cJSON* scan_and_get_ap_records(uint16_t *num_networks);

/**
 * Safely closes a socket with graceful shutdown attempt first, then force if needed
 * @param sock Socket file descriptor to close 
 * @return ESP_OK on success, ESP_FAIL on error
 */
static esp_err_t safe_close(int sock) ;

/**
 * @brief HTTP handler for test endpoint.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t test_handler(httpd_req_t *req);

/**
 * @brief Start the second web server.
 * 
 * @return httpd_handle_t Handle to the second web server.
 */
httpd_handle_t start_second_webserver(void);

/**
 * @brief Parse Wi-Fi credentials from HTTP request.
 * 
 * @param req HTTP request.
 * @param wifi_config Wi-Fi configuration to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config);

/**
 * @brief Wait for IP address to be assigned.
 * 
 * @param ip_info IP information to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info);

/**
 * @brief Create and bind a socket.
 * 
 * @param ip_info IP information for binding.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info);

/**
 * @brief HTTP handler to connect to a Wi-Fi network.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t connect_wifi_handler(httpd_req_t *req);

/**
 * @brief Get IP information of the access point.
 * 
 * @param ip_info IP information to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info);


/**
 * @brief Decrypts input data using a private key.
 *
 * This function uses the mbedtls library to decrypt data that was encrypted
 * with the corresponding public key. It initializes the necessary mbedtls
 * contexts, seeds the random number generator, parses the private key, and
 * performs the decryption.
 *
 * @param input Pointer to the encrypted input data.
 * @param input_len Length of the encrypted input data.
 * @param output Pointer to the buffer where the decrypted output data will be stored.
 * @param output_len Pointer to the size of the output buffer. On successful decryption,
 *                   this will be updated with the actual length of the decrypted data.
 *
 * @return 0 on success, or a non-zero error code on failure.
 */
int decrypt_with_private_key(unsigned char *input, size_t input_len, unsigned char *output, size_t *output_len) ;

/**
 * @brief Task to generate an RSA key pair.
 *
 * This function generates an RSA key pair using the mbedTLS library. It initializes the necessary
 * contexts, seeds the random number generator, and generates the RSA key pair. The public and private
 * keys are then written to PEM format and stored in the respective buffers. The watchdog timer is
 * disabled during the key generation process and re-enabled afterwards.
 *
 * @param pvParameters Pointer to the parameters passed to the task (not used).
 *
 * The function performs the following steps:
 * - Disables the watchdog timer.
 * - Initializes the mbedTLS contexts for PK, entropy, and CTR-DRBG.
 * - Seeds the CTR-DRBG context.
 * - Sets up the PK context for RSA.
 * - Generates the RSA key pair.
 * - Writes the public key to the public_key buffer in PEM format.
 * - Writes the private key to the private_key buffer in PEM format.
 * - Logs the generated keys.
 * - Frees the mbedTLS contexts.
 * - Re-enables the watchdog timer.
 * - Gives the semaphore to indicate that the key has been generated.
 * - Deletes the task.
 */
static void generate_key_pair_task(void *pvParameters);

/**
 * @brief Create and bind a socket for internal mode.
 * 
 * @param ip_info IP information for binding.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t create_and_bind_socket(esp_netif_ip_info_t *ip_info);

/**
 * @brief Handler function to send the public key in response to an HTTP request.
 *
 * This function is called when an HTTP request is received for the public key.
 * It sends the public key as the response.
 *
 * @param req Pointer to the HTTP request structure.
 */
esp_err_t get_public_key_handler(httpd_req_t *req);

/**
 * @brief Send internal mode response.
 * 
 * @param req HTTP request.
 * @param ip_str IP string.
 * @param new_port Port number.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
static esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port);

/**
 * @brief HTTP handler to switch to internal mode.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t internal_mode_handler(httpd_req_t *req);

/**
 * @brief Start the main web server.
 * 
 * @return httpd_handle_t Handle to the main web server.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Main application entry point.
 */
void app_main(void);

#endif // INCLUDE_H