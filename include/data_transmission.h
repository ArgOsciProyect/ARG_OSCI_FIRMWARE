/**
 * @file data_transmission.h
 * @brief Data transmission module for ESP32 oscilloscope
 *
 * Manages the transmission of acquired data samples over TCP sockets
 * to clients. This module handles socket connections, monitoring trigger
 * conditions, and sending data packets at the appropriate intervals.
 */

#ifndef DATA_TRANSMISSION_H
#define DATA_TRANSMISSION_H

#include <errno.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"

/**
 * @brief Initialize data transmission subsystem
 *
 * Sets up the necessary resources and state for data transmission.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_transmission_init(void);

/**
 * @brief Task to handle socket communication and data streaming
 *
 * This task accepts connections from clients, acquires data from ADC or SPI,
 * and streams the data to connected clients. It supports both continuous
 * and trigger-based acquisition modes. In external ADC mode, it also responds
 * to socket reset requests to safely close connections when needed.
 *
 * @param pvParameters Parameters for the task (unused)
 */
void socket_task(void *pvParameters);

/**
 * @brief Send data packet to connected client
 *
 * Transmits a buffer of data to the currently connected client over TCP.
 *
 * @param client_sock Socket descriptor for the connected client
 * @param buffer Data buffer to send
 * @param sample_size Size of each sample in bytes
 * @param discard_head Number of samples to discard from the start
 * @param samples_per_packet Number of samples to send
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t send_data_packet(int client_sock, uint8_t *buffer, size_t sample_size, int discard_head,
                           int samples_per_packet);

/**
 * @brief Switch to single trigger acquisition mode
 *
 * Configures the system to wait for and capture a single trigger event.
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t set_single_trigger_mode(void);

#ifdef USE_EXTERNAL_ADC
/**
 * @brief Request a reset of all socket connections
 *
 * Sets the socket_reset_requested flag to trigger the socket_task to close
 * any active client connections. This function waits for the flag to be processed
 * or times out after a delay.
 *
 * @note This function is only available in USE_EXTERNAL_ADC mode.
 */
void request_socket_reset(void);

/**
 * @brief Force immediate cleanup of all socket connections
 *
 * Requests socket_task to close client connections and also forcibly closes
 * the listening socket. This function is more aggressive than request_socket_reset
 * and ensures all socket resources are released promptly.
 *
 * @note This function is only available in USE_EXTERNAL_ADC mode.
 */
void force_socket_cleanup(void);

#endif

/**
 * @brief Switch to continuous acquisition mode
 *
 * Configures the system for continuous data acquisition.
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t set_continuous_mode(void);

/**
 * @brief Check if a trigger event has occurred
 *
 * Monitors input signal for a trigger event based on current
 * trigger settings (edge type and level).
 *
 * @param current Current signal value or edge state
 * @param last Previous signal value or edge state
 * @return true if a trigger event occurred, false otherwise
 */
bool is_triggered(int current, int last);

/**
 * @brief Send data packet to connected client in a non-blocking manner
 *
 * Transmits a buffer of data to the currently connected client over TCP in a non-blocking way.
 * In external ADC mode, also monitors for socket changes and reset requests during sending.
 *
 * @param client_sock Socket descriptor for the connected client
 * @param buffer Data buffer to send
 * @param len Length of the data to send
 * @param flags Flags for the send operation
 * @return ESP_OK on success, ESP_FAIL on error, ESP_ERR_TIMEOUT if a WiFi operation is requested
 */
esp_err_t non_blocking_send(int client_sock, void *buffer, size_t len, int flags);
/**
 * @brief Acquire data from the configured ADC
 *
 * Reads data from either the internal ADC (via adc_continuous_read) or
 * external ADC (via SPI) based on the current configuration. When using
 * external ADC, this function manages SPI mutex access.
 *
 * @param buffer Buffer to store the acquired data
 * @param buffer_size Size of the buffer in bytes
 * @param bytes_read Pointer to variable that will hold number of bytes read
 * @return ESP_OK on success, ESP_FAIL or error code on failure
 */
esp_err_t acquire_data(uint8_t *buffer, size_t buffer_size, uint32_t *bytes_read);

#endif /* DATA_TRANSMISSION_H */