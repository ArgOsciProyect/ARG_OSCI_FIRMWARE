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
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Main application entry point
 * 
 * Initializes all subsystems and starts the various tasks required
 * for the oscilloscope functionality.
 */
void app_main(void);

#endif /* MAIN_H */