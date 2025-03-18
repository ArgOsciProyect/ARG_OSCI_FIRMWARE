/**
 * @file data_transmission.c
 * @brief Implementation of data transmission functions
 */

#include "data_transmission.h"
#include "acquisition.h"
#include "globals.h"
#include "network.h"

static const char *TAG = "DATA_TRANS";

static uint8_t *pending_send_buffer = NULL;
static size_t pending_send_size = 0;
static size_t pending_send_offset = 0;
static bool send_in_progress = false;

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

#ifndef USE_EXTERNAL_ADC
atomic_int wifi_operation_requested = ATOMIC_VAR_INIT(0);
atomic_int wifi_operation_acknowledged = ATOMIC_VAR_INIT(0);
#endif

#ifdef USE_EXTERNAL_ADC
atomic_int socket_reset_requested = ATOMIC_VAR_INIT(0);
#endif
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
    t.rxlength = buffer_size * 8; // in bitsº
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
    int temp_last_state;
    pcnt_unit_get_count(pcnt_unit, &temp_last_state);
    last_state = temp_last_state;
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

#ifdef USE_EXTERNAL_ADC
void request_socket_reset(void)
{
    ESP_LOGI(TAG, "---------------------------------------------");
    ESP_LOGI(TAG, "SOCKET RESET: Setting socket_reset_requested flag");
    ESP_LOGI(TAG, "Previous flag value: %d", atomic_load(&socket_reset_requested));
    atomic_store(&socket_reset_requested, 1);
    ESP_LOGI(TAG, "Flag set to: %d", atomic_load(&socket_reset_requested));

    // Use a longer delay to ensure the task has time to process the request
    vTaskDelay(pdMS_TO_TICKS(350)); // Increased to 350ms

    // After delay, verify if flag is still set
    if (atomic_load(&socket_reset_requested)) {
        ESP_LOGW(TAG, "SOCKET RESET: Flag still set after delay - forcing cleanup");
        // Force cleanup in case socket_task is stuck
        atomic_store(&socket_reset_requested, 0);
    } else {
        ESP_LOGI(TAG, "SOCKET RESET: Flag was processed successfully");
    }
    ESP_LOGI(TAG, "---------------------------------------------");
}

void force_socket_cleanup(void)
{
    ESP_LOGI(TAG, "*** FORCE SOCKET CLEANUP: Closing all connections ***");

    // Close the client sockets first (from socket_task)
    atomic_store(&socket_reset_requested, 1);

    // Wait for socket_task to process the reset request
    vTaskDelay(pdMS_TO_TICKS(150));

    // Force close of the listening socket to ensure clean state
    if (new_sock != -1) {
        ESP_LOGI(TAG, "Forcing close of listening socket %d", new_sock);
        safe_close(new_sock);
        new_sock = -1;
    }

    // Make sure the reset flag is cleared
    atomic_store(&socket_reset_requested, 0);
    ESP_LOGI(TAG, "*** Force socket cleanup completed ***");
}
#endif

esp_err_t non_blocking_send(int client_sock, void *buffer, size_t len, int flags)
{
#ifdef USE_EXTERNAL_ADC
    static int socket_at_start = -1; // To track changes in new_sock during sending
#endif
    // If first send or previous send completed
    if (!send_in_progress) {
        // Store the buffer info for potential retries
        pending_send_buffer = buffer;
        pending_send_size = len;
        pending_send_offset = 0;
        send_in_progress = true;
#ifdef USE_EXTERNAL_ADC
        socket_at_start = new_sock; // Store the current value of new_sock
#endif
        // Make socket non-blocking for this operation
        int sock_flags = fcntl(client_sock, F_GETFL, 0);
        fcntl(client_sock, F_SETFL, sock_flags | O_NONBLOCK);
    }

    // Try to send remaining data
    while (pending_send_offset < pending_send_size) {
#ifndef USE_EXTERNAL_ADC
        // Only check wifi_operation_requested in internal ADC mode
        if (atomic_load(&wifi_operation_requested)) {
            // Reset socket to blocking mode before returning
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_ERR_TIMEOUT; // Signal caller to handle the WiFi operation
        }
#else
        // In external ADC mode, check if the socket has changed
        if (new_sock != socket_at_start) {
            ESP_LOGI(TAG, "Socket changed during send operation (was %d, now %d), aborting", socket_at_start, new_sock);
            // Reset socket to blocking mode before returning
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_FAIL; // Abort sending if the socket changed
        }

        // Also check for explicit socket reset requests
        if (atomic_load(&socket_reset_requested)) {
            ESP_LOGI(TAG, "Socket reset requested during send operation, aborting");
            // Reset socket to blocking mode before returning
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_FAIL;
        }
#endif

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
    int current_sock = -1; // To track changes in new_sock
    int client_sock = -1; // Declare client_sock at the beginning and set it to -1
    TickType_t last_heartbeat = 0;
    uint32_t loop_counter = 0;

#ifdef USE_EXTERNAL_ADC
    uint8_t buffer[BUF_SIZE];
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;
    t.rxlength = BUF_SIZE * 8;
    t.rx_buffer = buffer;
    t.flags = 0;
    len = BUF_SIZE;
    esp_err_t ret = ESP_OK; // Initialize ret to avoid the error
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
        // Only for internal ADC: WiFi operations check
        if (atomic_load(&wifi_operation_requested)) {
            ESP_LOGI(TAG, "WiFi operation requested, pausing ADC operations");

            if (atomic_load(&adc_is_running)) {
                stop_adc_sampling();
            }

            atomic_store(&wifi_operation_acknowledged, 1);

            while (atomic_load(&wifi_operation_requested)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            atomic_store(&wifi_operation_acknowledged, 0);

            ESP_LOGI(TAG, "Resuming ADC operations after WiFi change");
        }
#endif

#ifdef USE_EXTERNAL_ADC
        ESP_LOGD(TAG, "Socket task main loop - reset_flag:%d, new_sock:%d, current_sock:%d, client_sock:%d",
                 atomic_load(&socket_reset_requested), new_sock, current_sock, client_sock);
        // Check for socket reset more frequently
        if (atomic_load(&socket_reset_requested)) {
            ESP_LOGI(TAG, "---------------------------------------------");
            ESP_LOGI(TAG, "SOCKET RESET: Detected in main loop");
            ESP_LOGI(TAG, "Socket values - new:%d, current:%d, client:%d", new_sock, current_sock, client_sock);

            if (client_sock >= 0) {
                ESP_LOGI(TAG, "SOCKET RESET: Closing client socket %d due to reset request", client_sock);
                safe_close(client_sock);
                client_sock = -1;
                ESP_LOGI(TAG, "SOCKET RESET: Client socket closed successfully");
            } else {
                ESP_LOGI(TAG, "SOCKET RESET: No client socket to close");
            }

            atomic_store(&socket_reset_requested, 0);
            ESP_LOGI(TAG, "SOCKET RESET: Reset flag cleared");

            // Add delay to ensure clean state transition
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "---------------------------------------------");

            // Continue to restart from the beginning of the loop
            continue;
        }
#endif

        // Detect if the socket has changed
        if (new_sock != current_sock) {
            ESP_LOGI(TAG, "Detected socket change: previous=%d, new=%d", current_sock, new_sock);
            current_sock = new_sock;
            // If we were connected with a client, close that connection
            if (client_sock >= 0) {
                safe_close(client_sock);
                client_sock = -1;
                ESP_LOGI(TAG, "Closed previous client connection due to socket change");
            }
        }

        if (new_sock == -1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Make the listening socket non-blocking to be able to periodically
        // check if it has changed
        int sock_flags = fcntl(new_sock, F_GETFL, 0);
        fcntl(new_sock, F_SETFL, sock_flags | O_NONBLOCK);

        ESP_LOGI(TAG, "Waiting for client connection on socket %d...", new_sock);

        bool accept_completed = false;
        // Connection acceptance loop with timeouts to be able to detect changes
        while (!accept_completed) {
#ifdef USE_EXTERNAL_ADC
            // Check for socket reset request
            if (atomic_load(&socket_reset_requested)) {
                ESP_LOGI(TAG, "SOCKET RESET: Requested while waiting for connection");
                atomic_store(&socket_reset_requested, 0);
                accept_completed = true; // Exit the accept loop
                break;
            }
#endif

            // Check if the socket has changed
            if (new_sock != current_sock) {
                ESP_LOGI(TAG, "Socket changed while waiting for connection: old=%d, new=%d", current_sock, new_sock);
                accept_completed = true; // Exit the accept loop
                break;
            }

            client_sock = accept(new_sock, (struct sockaddr *)&client_addr, &client_addr_len);

            if (client_sock >= 0) {
                // Success, we have a connection
                inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
                ESP_LOGI(TAG, "Client connected: %s, Port: %d", addr_str, ntohs(client_addr.sin_port));
                accept_completed = true;
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // Real error in accept
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                close(new_sock);
                new_sock = -1;
                current_sock = -1; // Reset socket tracking
                accept_completed = true;
                break;
            }

            // No connection yet, wait a bit and keep checking
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        // If the socket changed or there was an error, go back to the beginning of the main loop
        if (new_sock != current_sock || client_sock < 0) {
            continue;
        }

        // Return to blocking mode for the listening socket
        sock_flags = fcntl(new_sock, F_GETFL, 0);
        fcntl(new_sock, F_SETFL, sock_flags & ~O_NONBLOCK);

#ifdef USE_EXTERNAL_ADC
        // We don't need to declare ret here since it's already declared and initialized above
#else
        if (!atomic_load(&adc_is_running) && !atomic_load(&adc_initializing)) {
            ESP_LOGI(TAG, "Starting ADC sampling from socket task");
            start_adc_sampling();
        } else {
            ESP_LOGW(TAG, "ADC already running or initializing, not starting again");
        }
#endif

        bool data_transfer_complete = false;
        loop_counter = 0;
        last_heartbeat = xTaskGetTickCount();

        while (!data_transfer_complete) {
            // Periodic heartbeat to check if task is still running
            if (++loop_counter % 5000 == 0) {
                TickType_t current_time = xTaskGetTickCount();
                if ((current_time - last_heartbeat) > pdMS_TO_TICKS(2000)) {
                    ESP_LOGI(TAG, "Data transfer heartbeat - still active, client:%d", client_sock);
                    last_heartbeat = current_time;
                }
            }

#ifndef USE_EXTERNAL_ADC
            // Only for internal ADC: WiFi operations check
            if (atomic_load(&wifi_operation_requested)) {
                data_transfer_complete = true;
                break; // Break the inner loop to handle this at the outer loop level
            }

            if (adc_modify_freq) {
                config_adc_sampling();
                adc_modify_freq = 0;
            }
#else
            // In external ADC mode, check if the socket has changed or reset is requested
            if (new_sock != current_sock || atomic_load(&socket_reset_requested)) {
                if (atomic_load(&socket_reset_requested)) {
                    ESP_LOGI(TAG, "---------------------------------------------");
                    ESP_LOGI(TAG, "SOCKET RESET: Requested during data transfer");
                    ESP_LOGI(TAG, "Socket values - new:%d, current:%d, client:%d", new_sock, current_sock, client_sock);
                    atomic_store(&socket_reset_requested, 0);
                    ESP_LOGI(TAG, "SOCKET RESET: Reset flag cleared");
                    ESP_LOGI(TAG, "---------------------------------------------");
                } else {
                    ESP_LOGI(TAG, "Socket changed during data transfer");
                }
                data_transfer_complete = true;
                break; // Exit the inner loop and go back to accept()
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

                // Use temporary variable to resolve compilation warning
                int temp_current_state;
                pcnt_unit_get_count(pcnt_unit, &temp_current_state);
                current_state = temp_current_state;

                if (last_state == current_state) {
                    continue;
                }
                last_state = current_state;

                if (ret == ESP_OK && len > 0) {
                    // Use pseudo-non-blocking send
                    esp_err_t send_result = non_blocking_send(client_sock, send_buffer, send_len, flags);
                    if (send_result != ESP_OK) {
                        ESP_LOGE(TAG, "Send error");
                        data_transfer_complete = true;
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
                        data_transfer_complete = true;
                        break; // Break the inner loop to handle WiFi operation
                    } else if (send_result != ESP_OK) {
                        ESP_LOGE(TAG, "Send error");
                        data_transfer_complete = true;
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
                // Use pseudo-non-blocking send
                esp_err_t send_result = non_blocking_send(client_sock, send_buffer, send_len, flags);
                if (send_result != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    data_transfer_complete = true;
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
                    data_transfer_complete = true;
                    break; // Break the inner loop to handle WiFi operation
                } else if (send_result != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    data_transfer_complete = true;
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

        if (client_sock >= 0) {
            safe_close(client_sock);
            client_sock = -1;
            ESP_LOGI(TAG, "Client disconnected");
        }
    }
}