/**
 * @file data_transmission.c
 * @brief Implementation of data transmission functions
 */

#include "data_transmission.h"
#include "acquisition.h"
#include "globals.h"
#include "network.h"

static const char *TAG = "DATA_TRANS";

/**
 * @brief Acquisition mode (0: continuous, 1: single trigger)
 */
atomic_int mode = ATOMIC_VAR_INIT(0);

/**
 * @brief Previous state of trigger input
 */
atomic_int last_state = ATOMIC_VAR_INIT(0);

/**
 * @brief Current state of trigger input
 */
atomic_int current_state = ATOMIC_VAR_INIT(0);

/**
 * @brief Trigger edge type (1: positive edge, 0: negative edge)
 */
atomic_int trigger_edge = ATOMIC_VAR_INIT(1);

// Add at the top of data_transmission.c with other atomic variables
atomic_int wifi_operation_requested = ATOMIC_VAR_INIT(0);
atomic_int wifi_operation_acknowledged = ATOMIC_VAR_INIT(0);

esp_err_t data_transmission_init(void)
{
    ESP_LOGI(TAG, "Initializing data transmission subsystem");
    read_miss_count = 0;
    return ESP_OK;
}

esp_err_t acquire_data(uint8_t *buffer, size_t buffer_size, uint32_t *bytes_read)
{
    esp_err_t ret = ESP_OK;

#ifdef USE_EXTERNAL_ADC
    // Configure SPI transaction
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;
    t.rxlength = buffer_size * 8; // in bits
    t.rx_buffer = buffer;
    t.flags = 0;

    // Take semaphore for SPI access
    if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
        // Perform SPI transaction
        ret = spi_device_polling_transmit(spi, &t);
        xSemaphoreGive(spi_mutex);

        if (ret == ESP_OK) {
            *bytes_read = buffer_size;
        } else {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
            *bytes_read = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to take SPI mutex");
        ret = ESP_FAIL;
        *bytes_read = 0;
    }
#else
    // Wait for ADC conversion
    vTaskDelay(pdMS_TO_TICKS(wait_convertion_time));

    // Read from ADC
    ret = adc_continuous_read(adc_handle, buffer, buffer_size, bytes_read, 1000 / portTICK_PERIOD_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
    }
#endif

    return ret;
}

bool is_triggered(int current, int last)
{
    // Check for the specific edge type set in trigger_edge
    if (trigger_edge == 1) {
        // Positive edge detection
        return (current > last);
    } else {
        // Negative edge detection
        return (current < last);
    }
}

esp_err_t set_single_trigger_mode(void)
{
    ESP_LOGI(TAG, "Entering single trigger mode");

    mode = 1; // Set to single trigger mode

#ifdef USE_EXTERNAL_ADC
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    if (trigger_edge == 1) {
        // Configure for positive edge detection
        ESP_ERROR_CHECK(
            pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    } else {
        // Configure for negative edge detection
        ESP_ERROR_CHECK(
            pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    }

    // Get initial state
    pcnt_unit_get_count(pcnt_unit, &last_state);
#else
    // Sample the current GPIO state
    last_state = gpio_get_level(SINGLE_INPUT_PIN);
#endif

    return ESP_OK;
}

esp_err_t set_continuous_mode(void)
{
    ESP_LOGI(TAG, "Entering continuous mode");

    mode = 0; // Set to continuous mode

    esp_err_t ret = set_trigger_level(0); // Reset trigger level
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set trigger level");
    }

#ifdef USE_EXTERNAL_ADC
    ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
#endif

    return ESP_OK;
}

esp_err_t send_data_packet(int client_sock, uint8_t *buffer, size_t sample_size, int discard_head,
                           int samples_per_packet)
{
    // Calculate actual data to send
    void *send_buffer = buffer + (discard_head * sample_size);
    size_t send_len = samples_per_packet * sample_size;

    // Use MSG_MORE to optimize TCP packet usage
    int flags = MSG_MORE;

    // Send the data
    ssize_t sent = send(client_sock, send_buffer, send_len, flags);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Socket buffer is full, need to wait
            ESP_LOGW(TAG, "Socket buffer full, waiting to send");
            vTaskDelay(pdMS_TO_TICKS(10));
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "Send error: errno %d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Add these variables to data_transmission.c (at the top with other global variables)
static uint8_t *pending_send_buffer = NULL;
static size_t pending_send_size = 0;
static size_t pending_send_offset = 0;
static bool send_in_progress = false;

// Non-blocking send function
esp_err_t non_blocking_send(int client_sock, void *buffer, size_t len, int flags)
{
    // If first send or previous send completed
    if (!send_in_progress) {
        // Store the buffer info for potential retries
        pending_send_buffer = buffer;
        pending_send_size = len;
        pending_send_offset = 0;
        send_in_progress = true;

        // Make socket non-blocking for this operation
        int sock_flags = fcntl(client_sock, F_GETFL, 0);
        fcntl(client_sock, F_SETFL, sock_flags | O_NONBLOCK);
    }

    // Try to send remaining data
    while (pending_send_offset < pending_send_size) {
        // Check for WiFi operation requests
        if (atomic_load(&wifi_operation_requested)) {
            // Reset socket to blocking mode before returning
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_ERR_TIMEOUT; // Signal caller to handle the WiFi operation
        }

        ssize_t sent = send(client_sock, pending_send_buffer + pending_send_offset,
                            pending_send_size - pending_send_offset, flags);

        if (sent > 0) {
            pending_send_offset += sent;
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer is full, wait a bit
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            // Actual error
            ESP_LOGE(TAG, "Send error: errno %d", errno);

            // Reset socket to blocking mode
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_FAIL;
        }
    }

    // Reset socket to blocking mode
    int sock_flags = fcntl(client_sock, F_GETFL, 0);
    fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
    send_in_progress = false;
    return ESP_OK;
}

void socket_task(void *pvParameters)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char addr_str[128];
    uint32_t len;

#ifdef USE_EXTERNAL_ADC
    uint8_t buffer[BUF_SIZE];
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;
    t.rxlength = BUF_SIZE * 8;
    t.rx_buffer = buffer;
    t.flags = 0;
    len = BUF_SIZE;
#else
    uint8_t buffer[BUF_SIZE];
#endif

// Calculate actual data to send
#ifdef USE_EXTERNAL_ADC
    size_t sample_size = sizeof(uint8_t);
#else
    size_t sample_size = sizeof(uint8_t);
#endif

    void *send_buffer = buffer + (get_discard_head() * sample_size);
    size_t send_len = get_samples_per_packet() * sample_size;

    int flags = MSG_MORE;

    while (1) {
#ifndef USE_EXTERNAL_ADC
        // Check for WiFi operation request at the beginning of each loop
        if (atomic_load(&wifi_operation_requested)) {
            ESP_LOGI(TAG, "WiFi operation requested, pausing ADC operations");

            // Safely stop ADC if it's running
            if (atomic_load(&adc_is_running)) {
                stop_adc_sampling();
            }

            // Acknowledge the request
            atomic_store(&wifi_operation_acknowledged, 1);

            // Wait until the WiFi operation is complete
            while (atomic_load(&wifi_operation_requested)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Reset acknowledgment
            atomic_store(&wifi_operation_acknowledged, 0);

            ESP_LOGI(TAG, "Resuming ADC operations after WiFi change");
        }
#endif

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

#ifdef USE_EXTERNAL_ADC
        esp_err_t ret;
#else
        start_adc_sampling();
#endif

        while (1) {
#ifndef USE_EXTERNAL_ADC
            // Check for WiFi operation request inside the inner loop as well
            if (atomic_load(&wifi_operation_requested)) {
                break; // Break the inner loop to handle this at the outer loop level
            }

            if (adc_modify_freq) {
                config_adc_sampling();
                adc_modify_freq = 0;
            }
#endif

            if (mode == 1) {
#ifdef USE_EXTERNAL_ADC
                if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
                    ret = spi_device_polling_transmit(spi, &t);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "SPI transaction failed");
                    }
                    xSemaphoreGive(spi_mutex);
                }

                pcnt_unit_get_count(pcnt_unit, &current_state);
                if (last_state == current_state) {
                    continue;
                }
                last_state = current_state;
                if (ret == ESP_OK && len > 0) {
                    // Prepare data for sending
                    ssize_t sent = send(client_sock, send_buffer, send_len, flags);
                    if (sent < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            vTaskDelay(pdMS_TO_TICKS(10));
                            continue;
                        }
                        ESP_LOGE(TAG, "Send error: errno %d", errno);
                        break;
                    }
                } else {
                    read_miss_count++;
                    ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                    if (read_miss_count >= 10) {
                        ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
                        read_miss_count = 0;
                    }
                }
                continue;
#else
                TickType_t xLastWakeTime = xTaskGetTickCount();
                current_state = gpio_get_level(SINGLE_INPUT_PIN);

                // Check for specific edge type
                bool edge_detected = (trigger_edge == 1) ? (current_state > last_state) : // Positive edge
                    (current_state < last_state); // Negative edge

                last_state = current_state;

                if (!edge_detected) {
                    continue;
                }

                // If we get here, edge was detected
                TickType_t xCurrentTime = xTaskGetTickCount();
                vTaskDelay(pdMS_TO_TICKS(wait_convertion_time / 2) - (xCurrentTime - xLastWakeTime));
                int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);
                if (ret == ESP_OK && len > 0) {
                    // Use non-blocking send
                    esp_err_t send_result = non_blocking_send(client_sock, send_buffer, send_len, flags);
                    if (send_result == ESP_ERR_TIMEOUT) {
                        break; // Break the inner loop to handle WiFi operation
                    } else if (send_result != ESP_OK) {
                        ESP_LOGE(TAG, "Send error");
                        break;
                    }
                } else {
                    read_miss_count++;
                    ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                    if (read_miss_count >= 10) {
                        ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
                        read_miss_count = 0;
                    }
                }
                continue;
#endif
            }

#ifdef USE_EXTERNAL_ADC
            if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
                ret = spi_device_polling_transmit(spi, &t);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "SPI transaction failed");
                }
                xSemaphoreGive(spi_mutex);
            }

            if (ret == ESP_OK && len > 0) {
                // Prepare data for sending
                ssize_t sent = send(client_sock, send_buffer, send_len, flags);
                if (sent < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    ESP_LOGE(TAG, "Send error: errno %d", errno);
                    break;
                }
            } else {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10) {
                    ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
                    read_miss_count = 0;
                }
            }
#else
            vTaskDelay(pdMS_TO_TICKS(wait_convertion_time));
            int ret = adc_continuous_read(adc_handle, buffer, BUF_SIZE, &len, 1000 / portTICK_PERIOD_MS);

            if (ret == ESP_OK && len > 0) {
                // Use non-blocking send for continuous mode
                esp_err_t send_result = non_blocking_send(client_sock, send_buffer, send_len, flags);
                if (send_result == ESP_ERR_TIMEOUT) {
                    break; // Break the inner loop to handle WiFi operation
                } else if (send_result != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    break;
                }
            } else {
                read_miss_count++;
                ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
                if (read_miss_count >= 10) {
                    ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
                    read_miss_count = 0;
                }
            }
#endif
        }

#ifndef USE_EXTERNAL_ADC
        stop_adc_sampling();
#endif

        safe_close(client_sock);
        ESP_LOGI(TAG, "Client disconnected");
    }
}