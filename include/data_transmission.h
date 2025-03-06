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
 * and trigger-based acquisition modes.
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
esp_err_t send_data_packet(int client_sock, uint8_t *buffer, size_t sample_size,
                           int discard_head, int samples_per_packet);

/**
 * @brief Switch to single trigger acquisition mode
 *
 * Configures the system to wait for and capture a single trigger event.
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t set_single_trigger_mode(void);

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
 * @brief Acquire data from the configured ADC
 *
 * Reads data from either the internal or external ADC based on
 * the current configuration.
 *
 * @param buffer Buffer to store the acquired data
 * @param buffer_size Size of the buffer in bytes
 * @param bytes_read Pointer to variable that will hold number of bytes read
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t acquire_data(uint8_t *buffer, size_t buffer_size,
                       uint32_t *bytes_read);

#endif /* DATA_TRANSMISSION_H */