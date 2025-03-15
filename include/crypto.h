/**
 * @file crypto.h
 * @brief Cryptography and security functions for ESP32 oscilloscope
 *
 * Implements RSA key generation, encryption/decryption, and handling of
 * secure communications for the oscilloscope device.
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <esp_err.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Decrypt data using the device's RSA private key
 *
 * Takes encrypted data and decrypts it using the device's RSA private key.
 * This function handles all the mbedTLS setup and teardown required.
 *
 * @param input Encrypted data buffer
 * @param input_len Length of encrypted data
 * @param output Buffer to store decrypted result
 * @param output_len Pointer to size of output buffer (updated with actual length)
 * @return 0 on success, mbedTLS error code on failure
 */
int decrypt_with_private_key(unsigned char *input, size_t input_len, unsigned char *output, size_t *output_len);

/**
 * @brief Decrypt a Base64-encoded encrypted message
 *
 * Decodes Base64 input then decrypts the binary data using RSA private key.
 * Used for secure communication with the web interface.
 *
 * @param encrypted_base64 Base64-encoded encrypted message
 * @param decrypted_output Buffer to store decrypted result
 * @param output_size Size of output buffer
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t decrypt_base64_message(const char *encrypted_base64, char *decrypted_output, size_t output_size);

/**
 * @brief Task that generates RSA key pair on startup
 *
 * Initializes cryptography contexts, generates RSA key pair, and stores
 * the keys in global buffers. Signals completion via semaphore.
 *
 * @param pvParameters Task parameters (unused)
 */
void generate_key_pair_task(void *pvParameters);

/**
 * @brief Initialize the key generation semaphore
 *
 * Creates the binary semaphore used to signal key generation completion.
 *
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t init_crypto(void);

/**
 * @brief Get the public key buffer
 *
 * @return Pointer to the buffer containing the public key in PEM format
 */
unsigned char *get_public_key(void);

/**
 * @brief Get the private key buffer
 *
 * @return Pointer to the buffer containing the private key in PEM format
 */
unsigned char *get_private_key(void);

/**
 * @brief Get the key generation semaphore
 *
 * @return Handle to the semaphore used to signal key generation completion
 */
SemaphoreHandle_t get_key_gen_semaphore(void);

/* Crypto Configuration */
#define KEYSIZE 3072

#endif /* CRYPTO_H */