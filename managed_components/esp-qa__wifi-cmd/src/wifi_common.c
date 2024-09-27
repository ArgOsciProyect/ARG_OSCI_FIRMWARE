/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"

#ifndef APP_TAG
#define APP_TAG "WIFI"
#endif

typedef struct {
    int8_t value;
    char *auth_mode;
} app_wifi_auth_mode_t;

static const app_wifi_auth_mode_t wifi_auth_mode_map[] = {
    {WIFI_AUTH_OPEN, "open"},
    {WIFI_AUTH_WEP, "wep"},
    {WIFI_AUTH_WPA_PSK, "wpa"},
    {WIFI_AUTH_WPA2_PSK, "wpa2"},
    {WIFI_AUTH_WPA_WPA2_PSK, "wpa_wpa2"},
    {WIFI_AUTH_WPA2_ENTERPRISE, "wpa2_enterprise"},
    {WIFI_AUTH_WPA3_PSK, "wpa3"},
    {WIFI_AUTH_WPA2_WPA3_PSK, "wpa2_wpa3"},
#ifdef WIFI_AUTH_WAPI_PSK
    {WIFI_AUTH_WAPI_PSK, "wapi"},
#endif
#ifdef WIFI_AUTH_OWE
    {WIFI_AUTH_OWE, "owe"},
#endif
};

#define WIFI_AUTH_TYPE_NUM  (sizeof(wifi_auth_mode_map) / sizeof(app_wifi_auth_mode_t))

uint8_t app_wifi_auth_mode_str2num(const char *auth_str)
{
    uint8_t auth_mode;
    for (auth_mode = 0; auth_mode < WIFI_AUTH_TYPE_NUM; auth_mode++) {
        if (strcmp(wifi_auth_mode_map[auth_mode].auth_mode, auth_str) == 0) {
            break;
        }
    }
    return wifi_auth_mode_map[auth_mode].value;
}

char *app_wifi_auth_mode_num2str(uint8_t value)
{
    uint8_t auth_mode;
    for (auth_mode = 0; auth_mode < WIFI_AUTH_TYPE_NUM; auth_mode++) {
        if (wifi_auth_mode_map[auth_mode].value == value) {
            break;
        }
    }
    assert(auth_mode != WIFI_AUTH_TYPE_NUM); /* If another authmode is added */
    return wifi_auth_mode_map[auth_mode].auth_mode;
}

wifi_interface_t app_wifi_interface_str2ifx(const char *interface)
{
    if (!strncmp(interface, "ap", 3)) {
        return WIFI_IF_AP;
    } else if (!strncmp(interface, "sta", 4)) {
        return WIFI_IF_STA;
    } else {
        ESP_LOGE(APP_TAG, "Can not get interface from str: %s", interface);
        /* Do not abort */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
        return WIFI_IF_MAX;
#else /* older IDF does not have IF_MAX*/
        return WIFI_IF_STA;
#endif
    }
    return WIFI_IF_STA;
}

typedef struct {
    char *mode_str;
    wifi_mode_t mode;
} app_wifi_mode_t;

static const app_wifi_mode_t s_wifi_mode_map[] = {
    {"null", WIFI_MODE_NULL},
    {"ap", WIFI_MODE_AP},
    {"sta", WIFI_MODE_STA},
    {"apsta", WIFI_MODE_APSTA},
};

#define WIFI_MODE_NUM  (sizeof(s_wifi_mode_map) / sizeof(app_wifi_mode_t))

wifi_mode_t app_wifi_mode_str2num(const char *mode_str)
{
    wifi_mode_t mode = 0;
    int i = 0;
    for (i = 0; i < WIFI_MODE_NUM; i++) {
        if (strcmp(s_wifi_mode_map[i].mode_str, mode_str) == 0) {
            mode = s_wifi_mode_map[i].mode;
            break;
        }
    }
    if (i == WIFI_MODE_NUM) {
        ESP_LOGE(APP_TAG, "Can not convert mode %s from str to value.", mode_str);
        /* Do not abort */
        return WIFI_MODE_NULL;
    }
    return mode;
}

const char *app_wifi_mode_num2str(const wifi_mode_t mode)
{
    char *mode_str = NULL;
    int i = 0;
    for (i = 0; i < WIFI_MODE_NUM; i++) {
        if (s_wifi_mode_map[i].mode == mode) {
            mode_str = s_wifi_mode_map[i].mode_str;
            break;
        }
    }
    if (i == WIFI_MODE_NUM) {
        ESP_LOGE(APP_TAG, "Can not convert mode %d to str", mode);
        /* Do not abort */
        return "unknown";
    }
    return mode_str;
}

esp_err_t wifi_cmd_str2mac(const char *str, uint8_t *mac_addr)
{
    unsigned int mac_tmp[6];
    if (6 != sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x%*c",
                    &mac_tmp[0], &mac_tmp[1], &mac_tmp[2],
                    &mac_tmp[3], &mac_tmp[4], &mac_tmp[5])) {
        return ESP_ERR_INVALID_MAC;
    }
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = (uint8_t)mac_tmp[i];
    }
    return ESP_OK;

}
