#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mbedtls/pk.h"
#include "mbedtls/aes.h"
#include "lwip/sockets.h"

#define GATTS_TAG "BLE_GATTS"
#define DEVICE_NAME "ESP32_BLE_SERVER"
#define GATTS_SERVICE_UUID   0x00FF
#define GATTS_CHAR_UUID      0xFF01
#define GATTS_DESCR_UUID     0x3333
#define GATTS_NUM_HANDLE     4

static uint8_t char_value[20] = {0};

#define MAX_MSG_LEN 128 // Ajusta según tus necesidades

static char last_message[MAX_MSG_LEN] = {0};
static SemaphoreHandle_t ble_message_mutex;
void init_ble();
void init_ble_wrapper();
void send_message_ble(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t char_handle, const uint8_t *message, size_t length);
const char* get_last_message();
void init_wifi_apsta();
void generate_rsa_keys();
void aes_encrypt_decrypt(const unsigned char *key, const unsigned char *input, unsigned char *output, int mode);
void start_socket_server();

static void gatts_event_handler(esp_gatts_cb_event_t event, 
                                esp_gatt_if_t gatts_if, 
                                esp_ble_gatts_cb_param_t *param);

void init_ble() {
    esp_err_t ret;
    // Initialize the BT Controller with default settings
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Bluetooth controller initialize failed: %s", esp_err_to_name(ret));
        return;
    }
    // Enable BT Controller in BLE mode
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return;
    }
    // Initialize the Bluedroid stack
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Bluedroid stack initialize failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Bluedroid stack enable failed: %s", esp_err_to_name(ret));
        return;
    }
    // Set the device name
    ret = esp_ble_gap_set_device_name(DEVICE_NAME);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return;
    }
    // Register the GATT server callback
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "GATT server register callback failed: %s", esp_err_to_name(ret));
        return;
    }
    // Register the application
    ret = esp_ble_gatts_app_register(0);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "GATT app register failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(GATTS_TAG, "BLE initialized successfully");
}

void init_ble_wrapper() {
    ble_message_mutex = xSemaphoreCreateMutex();
    if (ble_message_mutex == NULL) {
        ESP_LOGE("BLE_WRAPPER", "Failed to create mutex");
    }
}

void send_message_ble(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t char_handle, const uint8_t *message, size_t length) {
    // Copia los datos a un buffer mutable
    uint8_t temp_message[MAX_MSG_LEN] = {0};
    memcpy(temp_message, message, length);
    esp_err_t ret = esp_ble_gatts_send_indicate(
        gatts_if,          // Interfaz GATT
        conn_id,           // ID de conexión
        char_handle,       // Handle del atributo
        length,            // Longitud del mensaje
        temp_message,      // Puntero al mensaje
        false              // No requiere confirmación
    );
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Failed to send message: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(GATTS_TAG, "Message sent: %.*s", length, message);
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, 
                                esp_gatt_if_t gatts_if, 
                                esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_WRITE_EVT:
        if (param->write.is_prep == false) {
            xSemaphoreTake(ble_message_mutex, portMAX_DELAY);
            size_t len = param->write.len > MAX_MSG_LEN - 1 ? MAX_MSG_LEN - 1 : param->write.len;
            memcpy(last_message, param->write.value, len);
            last_message[len] = '\0'; // Asegura terminación de cadena
            xSemaphoreGive(ble_message_mutex);
            ESP_LOGI("BLE_WRAPPER", "Received message: %s", last_message);
        }
        break;
    default:
        break;
    }
}

const char* get_last_message() {
    static char safe_copy[MAX_MSG_LEN] = {0};
    
    xSemaphoreTake(ble_message_mutex, portMAX_DELAY);
    strncpy(safe_copy, last_message, MAX_MSG_LEN);
    xSemaphoreGive(ble_message_mutex);
    
    return safe_copy;
}


// --- Wi-Fi Configuration ---
void init_wifi_apsta() {
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();

    // Configure AP and STA settings
}

// --- RSA Key Pair Generation ---
void generate_rsa_keys() {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), NULL, NULL, 2048, 65537);
}

// --- AES Encryption and Decryption ---
void aes_encrypt_decrypt(const unsigned char *key, const unsigned char *input, unsigned char *output, int mode) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    mbedtls_aes_setkey_enc(&aes, key, 128); // Adjust for 128/192/256-bit keys
    if (mode == 1) { // Encrypt
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);
    } else { // Decrypt
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, output);
    }

    mbedtls_aes_free(&aes);
}

// --- TCP Socket Communication ---
void start_socket_server() {
    struct sockaddr_in server_addr;
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(sock, 5);

    // Accept connections and handle data
}

void send_ack() {
    if(get_last_message() == "ack") {
        send_message_ble(0, 0, 0, "ack", 3);
    }
}

void app_main() {
    nvs_flash_init();
    init_ble_wrapper();
    init_ble();
    init_wifi_apsta();
    generate_rsa_keys();
    start_socket_server();
}