/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "wifi_cmd.h"

#ifndef APP_TAG
#define APP_TAG "WIFI"
#endif

typedef enum {
    WIFI_ACTION_INIT = 0,
    WIFI_ACTION_START,
    WIFI_ACTION_STOP,
    WIFI_ACTION_DEINIT,
    WIFI_ACTION_RESTART,
    WIFI_ACTION_STATUS,
} wifi_action_t;

typedef struct {
    struct arg_str *action;
    struct arg_int *espnow_enc;
    struct arg_str *storage;
    struct arg_end *end;
} wifi_init_deinit_args_t;
static wifi_init_deinit_args_t wifi_args;

esp_netif_t *g_netif_ap = NULL;
esp_netif_t *g_netif_sta = NULL;
int g_wifi_connect_retry_cnt = 0;
wifi_init_config_t *g_wifi_cfg_p = NULL;

static bool s_wifi_initialized = false;

static esp_err_t app_initialise_netif_and_event_loop(void)
{
    static bool s_netif_initialized = false;
    if (s_netif_initialized) {
        ESP_LOGD(APP_TAG, "netif and event_loop already initialise.");
        return ESP_OK;
    }
    esp_netif_init();
    esp_event_loop_create_default();
    g_netif_ap = esp_netif_create_default_wifi_ap();
    assert(g_netif_ap);
    g_netif_sta = esp_netif_create_default_wifi_sta();
    assert(g_netif_sta);

    s_netif_initialized = true;
    return ESP_OK;
}

static esp_err_t app_wifi_init(wifi_init_config_t *cfg, wifi_storage_t *storage)
{
    app_initialise_netif_and_event_loop();

    esp_err_t ret = esp_wifi_init(cfg);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &app_handler_on_sta_disconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &app_handler_on_sta_connected, g_netif_sta));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &app_handler_on_scan_done, g_netif_sta));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_BEACON_TIMEOUT, &app_handler_on_bcn_timeout, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &app_handler_on_sta_got_ip, NULL));
#if CONFIG_LWIP_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &app_handler_on_sta_got_ipv6, NULL));
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_WIFI_CMD_ENABLE_ITWT
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_SETUP, &app_handler_on_itwt_setup, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_TEARDOWN, &app_handler_on_itwt_teardown, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_SUSPEND, &app_handler_on_itwt_suspend, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_PROBE, &app_handler_on_itwt_prob, NULL));
#endif /* CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_WIFI_CMD_ENABLE_ITWT*/

    if (storage != NULL) {
        ESP_ERROR_CHECK(esp_wifi_set_storage(*storage));
    }

#if CONFIG_WIFI_CMD_DEFAULT_COUNTRY_CN
    /* Only set country once during first initialize wifi */
    static bool country_code_has_set = false;
    if (country_code_has_set == false) {
        wifi_country_t country = {
            .cc = "CN",
            .schan = 1,
            .nchan = 13,
            .policy = 0
        };
        esp_wifi_set_country(&country);
        country_code_has_set = true;
    }
#endif
    return ret;
}

static esp_err_t app_wifi_deinit(void)
{
    esp_err_t ret = esp_wifi_deinit();

    /* Add a delay here to see if there is any event triggered during wifi deinit */
    vTaskDelay(1);

    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &app_handler_on_sta_disconnected));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &app_handler_on_sta_connected));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &app_handler_on_scan_done));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_BEACON_TIMEOUT, &app_handler_on_bcn_timeout));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &app_handler_on_sta_got_ip));
#if CONFIG_LWIP_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &app_handler_on_sta_got_ipv6));
#endif
    return ret;
}

/**
  * Used in app_main for simple wifi test apps.
  */
void app_initialise_wifi(app_wifi_initialise_config_t *config)
{
    if (s_wifi_initialized) {
        ESP_LOGW(APP_TAG, "WiFi already initialise.");
        return;
    }

    if (g_wifi_cfg_p == NULL) {
        g_wifi_cfg_p = (wifi_init_config_t *)malloc(sizeof(wifi_init_config_t));
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        memcpy(g_wifi_cfg_p, &cfg, sizeof(wifi_init_config_t));
    }

    ESP_ERROR_CHECK(app_wifi_init(g_wifi_cfg_p, NULL));
    if (config != NULL) {
        ESP_ERROR_CHECK(esp_wifi_set_storage(config->storage));
        ESP_ERROR_CHECK(esp_wifi_set_ps(config->ps_type));
    }
#if CONFIG_WIFI_CMD_INIT_TO_STATION
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif

    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_initialized = true;
}

esp_err_t app_wifi_operations(wifi_action_t action, wifi_storage_t *storage)
{
    esp_err_t ret = ESP_OK;
    switch (action) {
    case WIFI_ACTION_RESTART:
    /* fall through */
    case WIFI_ACTION_STOP:
        s_wifi_initialized = false;
        ret = esp_wifi_stop();
        WIFI_ERR_CHECK_LOG(ret, "esp_wifi_stop");
        if (action != WIFI_ACTION_RESTART) {
            break;
        }
    /* fall through */
    case WIFI_ACTION_DEINIT:
        ret = app_wifi_deinit();
        WIFI_ERR_CHECK_LOG(ret, "app_wifi_deinit");
        if (action != WIFI_ACTION_RESTART) {
            break;
        }
    /* fall through */
    case WIFI_ACTION_INIT:
        ret = app_wifi_init(g_wifi_cfg_p, storage);
        WIFI_ERR_CHECK_LOG(ret, "app_wifi_init");
        if (action != WIFI_ACTION_RESTART) {
            break;
        }
    /* fall through */
    case WIFI_ACTION_START:
        ret = esp_wifi_start();
        WIFI_ERR_CHECK_LOG(ret, "esp_wifi_start");
        s_wifi_initialized = true;
        break;
    default:
        break;
    }
    return ret;
}

static int cmd_do_set_wifi_init_deinit(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &wifi_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_args.end, argv[0]);
        return 1;
    }

    wifi_action_t action = 0;
    if (strcmp(wifi_args.action->sval[0], "init") == 0) {
        action = WIFI_ACTION_INIT;
    } else if (strcmp(wifi_args.action->sval[0], "deinit") == 0) {
        action = WIFI_ACTION_DEINIT;
    } else if (strcmp(wifi_args.action->sval[0], "start") == 0) {
        action = WIFI_ACTION_START;
    } else if (strcmp(wifi_args.action->sval[0], "stop") == 0) {
        action = WIFI_ACTION_STOP;
    } else if (strcmp(wifi_args.action->sval[0], "restart") == 0) {
        action = WIFI_ACTION_RESTART;
    } else if (strcmp(wifi_args.action->sval[0], "status") == 0) {
        action = WIFI_ACTION_STATUS;
    } else {
        ESP_LOGE(APP_TAG, "invaild input action!");
        return 1;
    }
    /* wifi status */
    if (action == WIFI_ACTION_STATUS) {
        ESP_LOGI(APP_TAG, "wifi initialized: %d", s_wifi_initialized);
        app_wifi_info_query();
        return 0;
    }

    if (wifi_args.espnow_enc->count > 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 4)
        g_wifi_cfg_p->espnow_max_encrypt_num = wifi_args.espnow_enc->ival[0];
        ESP_LOGI(APP_TAG, "set global wifi config espnow encrypt number: %d", g_wifi_cfg_p->espnow_max_encrypt_num);
#else
        ESP_LOGW(APP_TAG, "espnow enc number not supported <= v4.3");
#endif
    }
    wifi_storage_t *storage = NULL;
    if (wifi_args.storage->count > 0) {
        if (strcmp(wifi_args.storage->sval[0], "flash") == 0) {
            storage = (wifi_storage_t *)malloc(sizeof(wifi_storage_t));
            *storage = WIFI_STORAGE_FLASH;
        } else if (strcmp(wifi_args.storage->sval[0], "ram") == 0) {
            storage = (wifi_storage_t *)malloc(sizeof(wifi_storage_t));
            *storage = WIFI_STORAGE_RAM;
        } else {
            ESP_LOGE(APP_TAG, "invaild input storage, ignore!");
        }
    }

    esp_err_t err = app_wifi_operations(action, storage);
    if (storage) {
        free(storage);
    }

    LOG_WIFI_CMD_DONE(err, "WIFI");
    return 0;
}

void app_register_wifi_init_deinit(void)
{
    /* initialize g_wifi_cfg_p */
    if (g_wifi_cfg_p == NULL) {
        g_wifi_cfg_p = (wifi_init_config_t *)malloc(sizeof(wifi_init_config_t));
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        memcpy(g_wifi_cfg_p, &cfg, sizeof(wifi_init_config_t));
    }

    wifi_args.action = arg_str1(NULL, NULL, "<action>", "init; deinit; start; stop; restart; status;");
    wifi_args.espnow_enc = arg_int0(NULL, "espnow_enc", "<int>", "espnow encryption number (idf>=4.4), only for init and restart");
    wifi_args.storage = arg_str0(NULL, "storage", "<str>", "set wifi storage 'flash' or 'ram' during init");
    wifi_args.end = arg_end(2);
    const esp_console_cmd_t wifi_base_cmd = {
        .command = "wifi",
        .help = "Wi-Fi base operations: init, start, etc.",
        .hint = NULL,
        .func = &cmd_do_set_wifi_init_deinit,
        .argtable = &wifi_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_base_cmd));
}

esp_err_t app_register_all_wifi_commands(void)
{
    app_register_wifi_init_deinit();
    app_register_wifi_basic_commands();
    app_register_sta_basic_commands();
    app_register_ap_basic_commands();
#if CONFIG_WIFI_CMD_ENABLE_ITWT
    app_register_itwt_commands();
#endif
#if CONFIG_WIFI_CMD_ENABLE_WIFI_STATS
    app_register_wifi_stats_commands();
#endif
#if CONFIG_WIFI_CMD_ENABLE_BAND
    app_register_wifi_band_command();
#endif
    return ESP_OK;
}
