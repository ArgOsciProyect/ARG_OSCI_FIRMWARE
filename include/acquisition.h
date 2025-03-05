/**
 * @file acquisition.h
 * @brief Data acquisition module for ESP32 oscilloscope
 * 
 * This module handles data acquisition from either the ESP32's internal ADC
 * or an external ADC via SPI, depending on the configuration. It provides
 * functions for initializing, configuring and controlling various hardware
 * peripherals used in the oscilloscope application.
 */

#ifndef ACQUISITION_H
#define ACQUISITION_H

#include <esp_err.h>
#include <driver/spi_master.h>
#include <driver/mcpwm_prelude.h>
#include <driver/pulse_cnt.h>
#include "esp_adc/adc_continuous.h"
#include <driver/gpio.h>
#include <driver/timer.h>
#include <driver/ledc.h>
#include "driver/dac_cosine.h"
#include "driver/adc.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <stdatomic.h>

/**
 * @brief Initialize the SPI master for external ADC communication
 * 
 * Configures the SPI bus and device for communicating with an external ADC.
 * Sets up MISO pin, SPI bus, transaction queue, and initializes with settings
 * from spi_matrix[0]. Only available when USE_EXTERNAL_ADC is defined.
 */
void spi_master_init(void);

/**
 * @brief Initialize the MCPWM trigger for external ADC sampling
 * 
 * Configures MCPWM (Motor Control PWM) to generate precise timing
 * signals for external ADC triggering. Sets up timer, sync source,
 * operators, comparators, and generators. The PWM signal is synchronized
 * with an external trigger on SYNC_GPIO. Only used with external ADC.
 */
void init_mcpwm_trigger(void);

/**
 * @brief Initialize the pulse counter for edge detection
 * 
 * Sets up the PCNT (Pulse Counter) peripheral to detect edges for triggering.
 * Configures unit, channel, and filter settings. Initially set to count on 
 * positive edges. Used for edge-triggered acquisition with external ADC.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t init_pulse_counter(void);

/**
 * @brief Start continuous sampling with internal ADC
 * 
 * Configures and starts the ESP32's internal ADC in continuous mode.
 * Sets up sampling pattern, frequency, and buffer configuration.
 * Only available when USE_EXTERNAL_ADC is not defined.
 */
void start_adc_sampling(void);

/**
 * @brief Stop continuous sampling with internal ADC
 * 
 * Stops active ADC sampling and releases ADC resources.
 * Only available when USE_EXTERNAL_ADC is not defined.
 */
void stop_adc_sampling(void);

/**
 * @brief Update ADC sampling frequency
 * 
 * Reconfigures internal ADC with a new sampling frequency based on the
 * current adc_divider value. Stops and reinitializes the ADC with new settings.
 * Updates wait_convertion_time accordingly. Only available with internal ADC.
 */
void config_adc_sampling(void);

/**
 * @brief Configure GPIO input for trigger detection
 * 
 * Sets up SINGLE_INPUT_PIN as input with pull-down enabled for
 * detecting trigger events. Used in single-shot trigger mode.
 */
void configure_gpio(void);

/**
 * @brief Initialize hardware timer for precise timing
 * 
 * Configures Timer Group 0, Timer 0 for precise wait intervals.
 * Calculates appropriate wait_time_us based on sampling frequency
 * and buffer size. Used for timing in continuous acquisition mode.
 */
void my_timer_init(void);

/**
 * @brief Block until timer completes countdown
 * 
 * Starts the timer and waits until it reaches zero, then resets
 * for next use. Provides precise timing intervals for data acquisition.
 */
void timer_wait(void);

/**
 * @brief Initialize 1kHz square wave output
 * 
 * Configures LEDC to generate a 1kHz square wave with 50% duty cycle
 * on SQUARE_WAVE_GPIO. Used for testing and calibration purposes.
 */
void init_square_wave(void);

/**
 * @brief Initialize PWM for trigger level control
 * 
 * Sets up LEDC timer and channel to generate variable duty cycle
 * PWM on TRIGGER_PWM_GPIO. The PWM duty cycle controls the trigger
 * level reference voltage.
 */
void init_trigger_pwm(void);

/**
 * @brief Initialize sine wave generator using DAC
 * 
 * Configures the ESP32 DAC to generate a 10kHz sine wave.
 * Used for testing and calibration purposes.
 */
void init_sine_wave(void);

/**
 * @brief Task wrapper for sine wave initialization
 * 
 * Creates sine wave output on DAC channel 0 and then
 * deletes itself. This function is meant to be called as a FreeRTOS task.
 * 
 * @param pvParameters FreeRTOS task parameters (unused)
 */
void dac_sine_wave_task(void *pvParameters);

/**
 * @brief Set the trigger level reference voltage
 * 
 * Adjusts the duty cycle of the trigger PWM output to set a reference
 * voltage for the trigger comparator. The percentage value is converted
 * to a duty cycle in the range 0-1023 (10-bit resolution).
 * 
 * @param percentage Trigger level as percentage (0-100)
 * @return ESP_OK on success, ESP_FAIL on error or invalid percentage
 */
esp_err_t set_trigger_level(int percentage);

/**
 * @brief Get the configured sampling frequency
 * 
 * Returns the base sampling frequency before any divider is applied.
 * 2.5 MHz for external ADC, 494.753 kHz for internal ADC.
 * 
 * @return Sampling frequency in Hz
 */
double get_sampling_frequency(void);

/**
 * @brief Get hardware-specific dividing factor
 * 
 * Returns 1 for external ADC and 2 for internal ADC. This accounts for
 * hardware-specific differences in data representation.
 * 
 * @return Dividing factor value
 */
int dividing_factor(void);

/**
 * @brief Get the number of bits per data packet
 * 
 * @return Always returns 16 (bits per packet)
 */
int get_bits_per_packet(void);

/**
 * @brief Get the bit mask for extracting ADC data
 * 
 * Returns a bit mask to isolate actual data bits from the ADC reading.
 * 0x1FF8 for external ADC, 0x0FFF for internal ADC.
 * 
 * @return Bit mask for data extraction
 */
int get_data_mask(void);

/**
 * @brief Get the bit mask for extracting channel information
 * 
 * Returns a bit mask to isolate channel bits from the ADC reading.
 * 0x0 for external ADC, 0xF000 for internal ADC.
 * 
 * @return Bit mask for channel extraction
 */
int get_channel_mask(void);

/**
 * @brief Get the ADC resolution in bits
 * 
 * Returns the effective bit resolution of the ADC.
 * 10 bits for external ADC, ADC_BITWIDTH for internal ADC.
 * 
 * @return Number of useful bits in ADC reading
 */
int get_useful_bits(void);

/**
 * @brief Get number of samples to discard at beginning of buffer
 * 
 * @return Always returns 0 (no samples discarded from start)
 */
int get_discard_head(void);

/**
 * @brief Get number of samples to discard at end of buffer
 * 
 * @return Always returns 0 (no samples discarded from end)
 */
int get_discard_trailer(void);

/**
 * @brief Calculate effective number of samples per acquisition
 * 
 * Returns BUF_SIZE minus any discarded samples from head and trailer.
 * Since neither discard value is non-zero, this effectively returns BUF_SIZE.
 * 
 * @return Number of valid samples per acquisition
 */
int get_samples_per_packet(void);

/**
 * @brief Get the maximum possible ADC reading value
 * 
 * Returns 675 for external ADC, 1023 for internal ADC.
 * These values represent the maximum possible ADC output value.
 * 
 * @return Maximum ADC bit value
 */
int get_max_bits(void);

/**
 * @brief Get reference mid-point value for ADC readings
 * 
 * Returns 338 for external ADC, 512 for internal ADC.
 * These values represent a reference mid-point for signal display.
 * Always greater than half of get_max_bits().
 * 
 * @return Mid-point reference value
 */
int get_mid_bits(void);

#endif /* ACQUISITION_H */