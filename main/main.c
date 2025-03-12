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

    // Inicializar NVS (Non-Volatile Storage) para almacenamiento de
    // configuraciones
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Inicializar la pila de red y eventos del sistema
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Network stack initialized");

    esp_task_wdt_deinit(); // Asegurarse de que no hay una configuración anterior

    // Inicializar el subsistema criptográfico y generar claves RSA
    ESP_ERROR_CHECK(init_crypto());
    xTaskCreate(generate_key_pair_task, "generate_key_pair_task", 8192, NULL, 5, NULL);

    // Esperar a que se genere la clave (bloqueo hasta que se complete)
    if (xSemaphoreTake(get_key_gen_semaphore(), portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to wait for key generation");
        return;
    }
    ESP_LOGI(TAG, "RSA key pair generated successfully");

    // Configurar watchdog con un timeout largo para operaciones intensivas
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 1000000, // 1000 segundos
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Todos los núcleos
        .trigger_panic = false, // No causar pánico al expirar
    };

    // Inicializar generadores de señales para pruebas y calibración
    xTaskCreate(dac_sine_wave_task, "dac_sine_wave_task", 2048, NULL, 5, NULL);
    init_trigger_pwm(); // PWM para control de nivel de trigger
    init_square_wave(); // Onda cuadrada de calibración de 1KHz

    // Inicializar hardware específico según el tipo de ADC configurado
#ifdef USE_EXTERNAL_ADC
    // Configurar para ADC externo por SPI
    spi_mutex = xSemaphoreCreateMutex();
    if (spi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return;
    }
    spi_master_init(); // Inicializar interfaz SPI
    init_mcpwm_trigger(); // Configurar trigger preciso con MCPWM
    init_pulse_counter(); // Inicializar contador de pulsos para detección de
                          // flancos
    ESP_LOGI(TAG, "External ADC via SPI initialized");
#endif

    // Inicializar timer para sincronización precisa
    my_timer_init();
    ESP_LOGI(TAG, "Hardware timer initialized");

    // Configurar pin GPIO para entrada de trigger
    configure_gpio();
    ESP_LOGI(TAG, "TRIGGER GPIO pins configured");

    // Configurar pin GPIO para LED de estado
    configure_led_gpio();
    ESP_LOGI(TAG, "LED GPIO configured");

    // Inicializar WiFi en modo AP+STA
    wifi_init();
    ESP_LOGI(TAG, "WiFi initialized in AP+STA mode");

    // Iniciar el servidor HTTP primario (puerto 81)
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start primary HTTP server");
        return;
    }
    ESP_LOGI(TAG, "Primary HTTP server started on port 81");

    // Inicializar subsistema de transmisión de datos
    data_transmission_init();
    ESP_LOGI(TAG, "Data transmission subsystem initialized");

    // Crear la tarea principal para manejo de sockets en el núcleo 1
#ifdef USE_EXTERNAL_ADC
    xTaskCreatePinnedToCore(socket_task, "socket_task", 72000, NULL, 5, &socket_task_handle, 1);
#else
    xTaskCreatePinnedToCore(socket_task, "socket_task", 55000, NULL, 5, &socket_task_handle, 1);
#endif

    // Activar LED para indicar que el socket está listo para conexiones
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "Socket task created on core 1");

    // Iniciar tarea de monitoreo de memoria (opcional, comentada por defecto)
    // xTaskCreate(memory_monitor_task, "memory_monitor", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "ESP32 Oscilloscope initialization complete");
}