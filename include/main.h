/**
 * @file main.h
 * @brief Main application header for ESP32 oscilloscope
 *
 * Defines the main application entry point and core initialization
 * functions for the ESP32-based oscilloscope.
 */

#ifndef MAIN_H
#define MAIN_H

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

/**
 * @brief Main application entry point
 *
 * Initializes all subsystems and starts the various tasks required
 * for the oscilloscope functionality:
 * - NVS (Non-Volatile Storage) for configuration
 * - Network stack and event loop
 * - Cryptography subsystem and RSA key generation
 * - Signal generators (sine, square wave, PWM)
 * - ADC (internal or external via SPI)
 * - GPIO for triggers and status LEDs
 * - WiFi in AP+STA mode
 * - HTTP servers
 * - Data transmission subsystem
 */
void app_main(void);

#endif /* MAIN_H */