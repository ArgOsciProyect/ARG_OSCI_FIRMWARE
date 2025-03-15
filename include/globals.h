/**
 * @file globals.h
 * @brief Global definitions and shared variables for ESP32 oscilloscope
 *
 * Contains shared constants, configuration definitions, and extern declarations
 * for variables that need to be accessible across multiple modules.
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <driver/ledc.h>
#include <driver/mcpwm_prelude.h>
#include <driver/pulse_cnt.h>
#include <driver/spi_master.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdatomic.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"

// #define USE_EXTERNAL_ADC // Comment to use internal ADC

/* WiFi Configuration */
#define WIFI_SSID "ESP32_AP"
#define WIFI_PASSWORD "password123"
#define MAX_STA_CONN 4
#define PORT 8080

/* Crypto Configuration */
#define KEYSIZE 3072
#define KEYSIZEBITS 3072 * 8

/* Timer Configuration */
#define TIMER_DIVIDER 16
#define TIMER_BASE_CLK 80000000
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER)
#define TIMER_INTERVAL_US 2048

/* ADC Configuration */
#define ADC_CHANNEL ADC_CHANNEL_6
#define ADC_BITWIDTH ADC_WIDTH_BIT_10
#define SAMPLE_RATE_HZ 600000 /* 600 kHz */
#define WAIT_ADC_CONV_TIME 15

/* GPIO Definitions */
#define GPIO_INPUT_PIN GPIO_NUM_11
#define PIN_NUM_MISO 12
#define PIN_NUM_CLK 14
#define PIN_NUM_CS 15
#define MCPWM_GPIO 13
#define SYNC_GPIO 2
#define SQUARE_WAVE_GPIO GPIO_NUM_32
#define SINGLE_INPUT_PIN GPIO_NUM_33
#define TRIGGER_PWM_GPIO GPIO_NUM_27
#define LED_GPIO GPIO_NUM_25

/* SPI Configuration */
#define CS_CLK_TO_PWM 10
#define DELAY_NS 33
#define SPI_FREQ 40000000
#define PERIOD_TICKS 32
#define COMPARE_VALUE 26
#define SPI_FREQ_SCALE_FACTOR 1000 / 16
#define MATRIX_SPI_ROWS 7
#define MATRIX_SPI_COLS 5

/* Signal Generation */
#define SQUARE_WAVE_FREQ 1000 /* 1 KHz */
#define SQUARE_WAVE_TIMER LEDC_TIMER_1
#define SQUARE_WAVE_CHANNEL LEDC_CHANNEL_1
#define TRIGGER_PWM_FREQ 78125
#define TRIGGER_PWM_TIMER LEDC_TIMER_0
#define TRIGGER_PWM_CHANNEL LEDC_CHANNEL_0
#define TRIGGER_PWM_RES LEDC_TIMER_10_BIT
#define MCPWM_FREQ_HZ 2500000 /* 2.5MHz */

/* Pulse Counter */
#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_HIGH_LIMIT INT16_MAX
#define PCNT_LOW_LIMIT INT16_MIN

/* Buffer Configuration */
#ifdef USE_EXTERNAL_ADC
#define BUF_SIZE 17280 * 4
#else
#define BUF_SIZE 17280 * 3
#endif

/* Heap Tracing */
#ifdef CONFIG_HEAP_TRACING
#define HEAP_TRACE_ITEMS 100
#endif

/* External Global Variables */
extern adc_continuous_handle_t adc_handle;
extern atomic_int adc_modify_freq;
extern atomic_int adc_divider;
extern int read_miss_count;
extern int wait_convertion_time;
extern spi_device_handle_t spi;
extern mcpwm_timer_handle_t timer;
extern mcpwm_oper_handle_t oper;
extern mcpwm_cmpr_handle_t comparator;
extern mcpwm_gen_handle_t generator;
extern const uint32_t spi_matrix[MATRIX_SPI_ROWS][MATRIX_SPI_COLS];
extern int spi_index;
extern ledc_channel_config_t ledc_channel;
extern atomic_int mode;
extern atomic_int last_state;
extern atomic_int trigger_edge;
extern atomic_int current_state;
extern pcnt_unit_handle_t pcnt_unit;
extern pcnt_channel_handle_t pcnt_chan;
extern int new_sock;
extern httpd_handle_t second_server;
extern TaskHandle_t socket_task_handle;
extern unsigned char public_key[KEYSIZE];
extern unsigned char private_key[KEYSIZE];
extern SemaphoreHandle_t key_gen_semaphore;
extern uint64_t wait_time_us;
extern atomic_int wifi_operation_requested;
extern atomic_int wifi_operation_acknowledged;
extern atomic_bool adc_is_running;

#ifdef USE_EXTERNAL_ADC
extern SemaphoreHandle_t spi_mutex;
#endif

/* Define SPI matrix content */
#define MATRIX_SPI_FREQ                                                                   \
    {{40000000, CS_CLK_TO_PWM, DELAY_NS, PERIOD_TICKS, COMPARE_VALUE},                    \
     {20000000, CS_CLK_TO_PWM - 2, DELAY_NS + 13, PERIOD_TICKS * 2, COMPARE_VALUE * 2},   \
     {10000000, CS_CLK_TO_PWM - 3, DELAY_NS + 38, PERIOD_TICKS * 4, COMPARE_VALUE * 4},   \
     {5000000, CS_CLK_TO_PWM - 3, DELAY_NS + 188, PERIOD_TICKS * 8, COMPARE_VALUE * 8},   \
     {2500000, CS_CLK_TO_PWM - 3, DELAY_NS + 88, PERIOD_TICKS * 16, COMPARE_VALUE * 16},  \
     {1250000, CS_CLK_TO_PWM - 3, DELAY_NS + 288, PERIOD_TICKS * 32, COMPARE_VALUE * 32}, \
     {625000, CS_CLK_TO_PWM - 3, DELAY_NS + 788, PERIOD_TICKS * 64, COMPARE_VALUE * 64}}

#endif /* GLOBALS_H */