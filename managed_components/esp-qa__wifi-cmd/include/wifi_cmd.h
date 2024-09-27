/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"

/*
 * CONFIG_ESP_WIFI_ENABLED added in idf v5.1
 * define it in header file to simplify the application code
 */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#define CONFIG_ESP_WIFI_ENABLED (1)
#elif ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 1, 0)
#ifdef CONFIG_ESP32_WIFI_ENABLED
#define CONFIG_ESP_WIFI_ENABLED (CONFIG_ESP32_WIFI_ENABLED)
#endif
#endif

#if CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED

/* Avoid re-connect forever */
#define DEF_WIFI_CONN_MAX_RETRY_CNT (30)

typedef struct {
    bool reconnect;
    uint32_t reconnect_max_retry;
} wifi_cmd_config_t;

#define WIFI_CMD_CONFIG_DEFAULT() { \
    .reconnect = true, \
    .reconnect_max_retry = DEF_WIFI_CONN_MAX_RETRY_CNT, \
}

/* TODO: v1.0 remove this structure and use wifi_init_config_t directly */
typedef struct {
    wifi_storage_t storage;
    wifi_ps_type_t ps_type;
} app_wifi_initialise_config_t;

typedef struct {
    int bcn_timeout;
    int sta_disconnected;
} app_wifi_count_t;

#define APP_WIFI_CONFIG_DEFAULT() { \
    .storage = WIFI_STORAGE_FLASH, \
    .ps_type = WIFI_PS_MIN_MODEM, \
}

#define LOG_WIFI_CMD_DONE(ret, desc) do { \
    if (ret == ESP_OK) { \
        ESP_LOGI(APP_TAG, "DONE.%s,OK.", desc); \
    } else { \
        ESP_LOGI(APP_TAG, "DONE.%s,FAIL.%d,%s", desc, ret, esp_err_to_name(ret)); \
    } \
} while(0)

#define WIFI_ERR_CHECK_LOG(ret, desc) do { \
    if (ret != ESP_OK) { \
        ESP_LOGW(APP_TAG, "@EW:failed:%s,%d,%s", desc, ret, esp_err_to_name(ret)); \
    } \
} while(0)

/**
 * Variables
 */
extern app_wifi_count_t g_wcount;
extern esp_netif_t *g_netif_ap;
extern esp_netif_t *g_netif_sta;
extern wifi_cmd_config_t g_wifi_cmd_config;
extern bool g_is_sta_wifi_connected;
extern bool g_is_sta_got_ip4;
extern bool g_is_scan_count_only;

/**
 * @brief This function is a combination of "wifi init" and "wifi start".
 *
 * @param[in] config Specifies the WiFi initialization configuration that differs from the sdkconfig default, can be NULL.
 *                   wifi-cmd will save this configuration globally and use it during "wifi restart".
 */
void app_initialise_wifi(app_wifi_initialise_config_t *config);

/**
 * include: wifi init;deinit;start;stop;restart;status;
 */
void app_register_wifi_init_deinit(void);

/**
 * include: wifi_count [show/clear]
 */
void app_register_wifi_count_commands(void);

/**
 * include: wifi, wifi_count, wifi_mode, wifi_protocol, wifi_bandwidth, wifi_ps
 */
void app_register_wifi_basic_commands(void);

/**
 * include: sta_connect, sta_disconnect
 */
void app_register_sta_basic_commands(void);

/**
 * include: ap_set, ap_status
 */
void app_register_ap_basic_commands(void);

/**
 * include: wifi_band
 */
void app_register_wifi_band_command(void);

/**
 * include: all wifi commands
 */
esp_err_t app_register_all_wifi_commands(void);

#if CONFIG_WIFI_CMD_ENABLE_ITWT
/**
 * include: itwt, probe
 */
void app_register_itwt_commands(void);
#endif /* CONFIG_WIFI_CMD_ENABLE_ITWT */

#if CONFIG_WIFI_CMD_ENABLE_WIFI_STATS
/**
 * include: txstats, rxstats
 */
void app_register_wifi_stats_commands(void);
#endif /* CONFIG_WIFI_CMD_ENABLE_WIFI_STATS */

#if CONFIG_WIFI_CMD_ENABLE_HE_DEBUG
/**
 * include: he debug commands
 */
void app_register_wifi_he_debug_commands(void);
#endif /* WIFI_CMD_ENABLE_HE_DEBUG */

/* internal */
extern int g_wifi_connect_retry_cnt;
uint8_t app_wifi_auth_mode_str2num(const char *auth_str);
char *app_wifi_auth_mode_num2str(uint8_t value);
wifi_interface_t app_wifi_interface_str2ifx(const char *interface);
uint32_t app_wifi_protocol_str2bitmap(const char *protocol_str);
const char *app_wifi_protocol_bitmap2str(const uint32_t bitmap);
wifi_mode_t app_wifi_mode_str2num(const char *mode_str);
const char *app_wifi_mode_num2str(const wifi_mode_t mode);
void app_wifi_info_query(void);
bool wifi_cmd_str2mac(const char *str, uint8_t *mac_addr);
void app_register_wifi_protocol(void);
void app_register_wifi_bandwidth(void);

/* wifi handlers */
void app_handler_on_sta_disconnected(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_sta_connected(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_scan_done(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_sta_got_ipv6(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_bcn_timeout(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_WIFI_CMD_ENABLE_ITWT
void app_handler_on_itwt_setup(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_itwt_teardown(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_itwt_suspend(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void app_handler_on_itwt_prob(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
#endif /* CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_WIFI_CMD_ENABLE_ITWT*/

#endif  /* CONFIG_ESP_WIFI_ENABLED */
