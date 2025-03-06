/**
 * @file crypto.c
 * @brief Implementation of cryptography and security functions
 */

#include "crypto.h"
#include "globals.h"

static const char *TAG = "CRYPTO";

// Definición de variables globales declaradas como externas en globals.h
unsigned char public_key[KEYSIZE];
unsigned char private_key[KEYSIZE];
SemaphoreHandle_t key_gen_semaphore = NULL;

esp_err_t init_crypto(void)
{
    // Crear el semáforo binario para la generación de claves
    key_gen_semaphore = xSemaphoreCreateBinary();
    if (key_gen_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create key generation semaphore");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void generate_key_pair_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Generating RSA key pair...");
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "gen_key_pair";

    // Inicializar los contextos de mbedtls
    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Sembrar el generador de números aleatorios
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers,
                                     strlen(pers))) != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        goto exit;
    } else {
        ESP_LOGI(TAG, "mbedtls_ctr_drbg_seed successful");
    }

    // Configurar el contexto PK para RSA
    if ((ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_setup returned %d", ret);
        goto exit;
    } else {
        ESP_LOGI(TAG, "mbedtls_pk_setup successful");
    }

    // Generar el par de claves RSA
    ESP_LOGI(TAG, "Starting key generation (this may take several minutes)...");
    if ((ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg, KEYSIZE, 65537)) != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_gen_key returned %d", ret);
        goto exit;
    } else {
        ESP_LOGI(TAG, "Key generation successful");
    }

    // Escribir la clave pública en formato PEM
    memset(public_key, 0, sizeof(public_key));
    if ((ret = mbedtls_pk_write_pubkey_pem(&pk, public_key, sizeof(public_key))) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_write_pubkey_pem returned %d", ret);
        goto exit;
    } else {
        ESP_LOGI(TAG, "Public key successfully written");
    }

    // Escribir la clave privada en formato PEM
    memset(private_key, 0, sizeof(private_key));
    if ((ret = mbedtls_pk_write_key_pem(&pk, private_key, sizeof(private_key))) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_write_key_pem returned %d", ret);
        goto exit;
    } else {
        ESP_LOGI(TAG, "Private key successfully written");
    }

exit:
    // Liberar los recursos de mbedtls
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    // Dar el semáforo para indicar que la generación de claves ha terminado
    xSemaphoreGive(key_gen_semaphore);

    // Eliminar la tarea
    vTaskDelete(NULL);
}

int decrypt_with_private_key(unsigned char *input, size_t input_len, unsigned char *output, size_t *output_len)
{
    int ret;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "decrypt";

    // Inicializar los contextos de mbedtls
    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Sembrar el generador de números aleatorios
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers,
                                     strlen(pers))) != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        goto exit;
    }

    // Analizar la clave privada
    if ((ret = mbedtls_pk_parse_key(&pk, private_key, strlen((char *)private_key) + 1, NULL, 0, mbedtls_ctr_drbg_random,
                                    &ctr_drbg)) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_parse_key returned %d", ret);
        goto exit;
    }

    // Descifrar los datos
    size_t max_output_len = *output_len; // Asumir que *output_len es el tamaño del buffer
    if ((ret = mbedtls_pk_decrypt(&pk, input, input_len, output, output_len, max_output_len, mbedtls_ctr_drbg_random,
                                  &ctr_drbg)) != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_decrypt returned %d", ret);
        ESP_LOGE(TAG, "output_len: %d", *output_len);
        goto exit;
    }

exit:
    // Liberar los recursos de mbedtls
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    return ret;
}

esp_err_t decrypt_base64_message(const char *encrypted_base64, char *decrypted_output, size_t output_size)
{
    // Decodificar Base64
    unsigned char decoded[512];
    size_t decoded_len;

    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len, (unsigned char *)encrypted_base64,
                                    strlen(encrypted_base64));
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        return ESP_FAIL;
    }

    // Descifrar
    size_t decrypted_len = output_size;
    ret = decrypt_with_private_key(decoded, decoded_len, (unsigned char *)decrypted_output, &decrypted_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Decryption failed: %d", ret);
        return ESP_FAIL;
    }

    // Asegurar terminación nula
    decrypted_output[decrypted_len] = '\0';

    return ESP_OK;
}

unsigned char *get_public_key(void)
{
    return public_key;
}

unsigned char *get_private_key(void)
{
    return private_key;
}

SemaphoreHandle_t get_key_gen_semaphore(void)
{
    return key_gen_semaphore;
}