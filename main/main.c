/**
 * @file main.c
 * @brief Main application implementation for ESP32 oscilloscope
 */

#include "main.h"
#include "acquisition.h"
#include "crypto.h"
#include "data_transmission.h"
#include "globals.h"
#include "network.h"
#include "webservers.h"

static const char *TAG = "MAIN";

TaskHandle_t socket_task_handle; /**< Handle to the socket communication task */

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing ESP32 Oscilloscope");

    // Initialize NVS (Non-Volatile Storage) for storing configurations
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize the network stack and system events
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Network stack initialized");

    esp_task_wdt_deinit(); // Ensure no previous configuration exists

    // Initialize the cryptographic subsystem and generate RSA keys
    ESP_ERROR_CHECK(init_crypto());
    xTaskCreate(generate_key_pair_task, "generate_key_pair_task", 8192, NULL, 5, NULL);

    // Wait for key generation to complete (blocking)
    if (xSemaphoreTake(get_key_gen_semaphore(), portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to wait for key generation");
        return;
    }
    ESP_LOGI(TAG, "RSA key pair generated successfully");

    // Configure watchdog with a long timeout for intensive operations
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 1000000, // 1000 seconds
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // All cores
        .trigger_panic = false, // Do not cause panic on timeout
    };

    // Initialize signal generators for testing and calibration
    xTaskCreate(dac_sine_wave_task, "dac_sine_wave_task", 2048, NULL, 5, NULL);
    init_trigger_pwm(); // PWM for trigger level control
    init_square_wave(); // 1KHz square wave for calibration

    // Initialize hardware specific to the configured ADC type
#ifdef USE_EXTERNAL_ADC
    // Configure for external ADC via SPI
    spi_mutex = xSemaphoreCreateMutex();
    if (spi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return;
    }
    spi_master_init(); // Initialize SPI interface
    init_mcpwm_trigger(); // Configure precise trigger with MCPWM
    init_pulse_counter(); // Initialize pulse counter for edge detection
    ESP_LOGI(TAG, "External ADC via SPI initialized");
#endif

    // Initialize timer for precise synchronization
    my_timer_init();
    ESP_LOGI(TAG, "Hardware timer initialized");

    // Configure GPIO pin for trigger input
    configure_gpio();
    ESP_LOGI(TAG, "TRIGGER GPIO pins configured");

    // Configure GPIO pin for status LED
    configure_led_gpio();
    ESP_LOGI(TAG, "LED GPIO configured");

    // Initialize WiFi in AP+STA mode
    wifi_init();
    ESP_LOGI(TAG, "WiFi initialized in AP+STA mode");

    // Start the primary HTTP server (port 81)
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start primary HTTP server");
        return;
    }
    ESP_LOGI(TAG, "Primary HTTP server started on port 81");

    // Initialize data transmission subsystem
    data_transmission_init();
    ESP_LOGI(TAG, "Data transmission subsystem initialized");

    // Create the main task for socket handling on core 1
#ifdef USE_EXTERNAL_ADC
    xTaskCreatePinnedToCore(socket_task, "socket_task", 72000, NULL, 5, &socket_task_handle, 1);
#else
    xTaskCreatePinnedToCore(socket_task, "socket_task", 55000, NULL, 5, &socket_task_handle, 1);
#endif

    // Activate LED to indicate socket is ready for connections
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "Socket task created on core 1");

    // Start memory monitoring task (optional, commented out by default)
    // xTaskCreate(memory_monitor_task, "memory_monitor", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "ESP32 Oscilloscope initialization complete");
}