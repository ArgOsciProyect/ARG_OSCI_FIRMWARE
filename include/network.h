/**
 * @file network.h
 * @brief Network and WiFi functionality for ESP32 oscilloscope
 *
 * This module handles WiFi initialization, connection management,
 * socket operations, and network scanning capabilities for the
 * ESP32 oscilloscope application.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <cJSON.h>
#include <errno.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize WiFi in AP+STA mode
 *
 * Configures and starts the WiFi subsystem in both Access Point and
 * Station modes. Creates the ESP32_AP access point and prepares for
 * potential connections to external networks.
 */
void wifi_init(void);

/**
 * @brief Safely close a socket connection
 *
 * Attempts to gracefully shut down a socket connection with timeout.
 * Falls back to forced closure if graceful shutdown fails.
 *
 * @param sock Socket file descriptor to close
 * @return ESP_OK on successful closure, ESP_FAIL on error
 */
esp_err_t safe_close(int sock);

/**
 * @brief Get IP information for the ESP32's access point interface
 *
 * Retrieves IP address, gateway, and netmask information for the
 * ESP32's access point interface.
 *
 * @param ip_info Pointer to structure to store the IP information
 * @return ESP_OK on success, ESP_FAIL if information cannot be retrieved
 */
esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info);

/**
 * @brief Wait for IP address assignment in station mode
 *
 * Polls periodically for an IP address assignment when connected to
 * an external WiFi network in station mode. Times out after 10 attempts.
 *
 * @param ip_info Pointer to structure to store the IP information
 * @return ESP_OK when IP is obtained, ESP_FAIL on timeout
 */
esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info);

/**
 * @brief Create, bind, and start listening on a socket
 *
 * Creates a TCP socket, binds it to the specified IP address with
 * a dynamic port, and sets it to the listening state.
 *
 * @param ip_info IP information to bind the socket to
 * @return ESP_OK on success, ESP_FAIL on any error
 */
esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info);

/**
 * @brief Add a unique WiFi network SSID to a JSON array
 *
 * Helper function that adds a WiFi access point's SSID to a JSON array
 * if it's not already present in the array.
 *
 * @param root JSON array to add the SSID to
 * @param ap_record WiFi access point record containing the SSID
 */
void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record);

/**
 * @brief Scan for WiFi networks and return a JSON array of unique SSIDs
 *
 * Performs a WiFi scan and creates a JSON array containing all unique
 * SSIDs discovered in the vicinity.
 *
 * @param num_networks Pointer to store the number of networks found
 * @return JSON array of unique SSIDs, NULL on failure
 */
cJSON *scan_and_get_ap_records(uint16_t *num_networks);

#endif /* NETWORK_H */