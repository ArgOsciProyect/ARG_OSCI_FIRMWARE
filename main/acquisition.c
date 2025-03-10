/**
 * @file acquisition.c
 * @brief Implementation of data acquisition functionality
 */

#include "acquisition.h"
#include "globals.h"

static const char *TAG = "ACQUISITION";

// Definiciones de variables globales declaradas como externas en globals.h
adc_continuous_handle_t adc_handle;
atomic_int adc_modify_freq = 0;
atomic_int adc_divider = 1;
int read_miss_count = 0;
int wait_convertion_time = WAIT_ADC_CONV_TIME;
spi_device_handle_t spi;
mcpwm_timer_handle_t timer = NULL;
mcpwm_oper_handle_t oper = NULL;
mcpwm_cmpr_handle_t comparator = NULL;
mcpwm_gen_handle_t generator = NULL;
const uint32_t spi_matrix[MATRIX_SPI_ROWS][MATRIX_SPI_COLS] = MATRIX_SPI_FREQ;
int spi_index = 0;
ledc_channel_config_t ledc_channel;
uint64_t wait_time_us;
pcnt_unit_handle_t pcnt_unit;
pcnt_channel_handle_t pcnt_chan;

#ifdef USE_EXTERNAL_ADC
SemaphoreHandle_t spi_mutex = NULL;

void spi_master_init(void)
{
    esp_err_t ret;
    int freq;

    // Configurar el pin MISO como entrada con pull-down
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << PIN_NUM_MISO),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_ENABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configurar el bus SPI
    spi_bus_config_t buscfg = {.miso_io_num = PIN_NUM_MISO,
                               .mosi_io_num = -1, // No se usa MOSI
                               .sclk_io_num = PIN_NUM_CLK,
                               .quadwp_io_num = -1,
                               .quadhd_io_num = -1,
                               .max_transfer_sz = 2 * BUF_SIZE,
                               .flags =
                                   SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MISO | SPICOMMON_BUSFLAG_IOMUX_PINS,
                               .intr_flags = ESP_INTR_FLAG_IRAM};

    // Inicializar el bus SPI
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 3);
    ESP_ERROR_CHECK(ret);

    // Configurar el dispositivo SPI
    spi_device_interface_config_t devcfg = {.clock_speed_hz = spi_matrix[0][0], // Velocidad inicial del reloj SPI
                                            .mode = 0, // Modo SPI 0
                                            .spics_io_num = PIN_NUM_CS, // Pin CS
                                            .queue_size = 7, // Tamaño de la cola de transacciones
                                            .pre_cb = NULL, // No hay callback pre-transacción
                                            .post_cb = NULL, // No hay callback post-transacción
                                            .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
                                            .cs_ena_pretrans = spi_matrix[0][1],
                                            .input_delay_ns = spi_matrix[0][2]};

    // Inicializar el dispositivo SPI
    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master initialized");
    spi_device_get_actual_freq(spi, &freq);
    ESP_LOGI(TAG, "Actual SPI frequency: %d Hz", freq);
}

void init_mcpwm_trigger(void)
{
    // Configurar el pin de sincronización como entrada
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << SYNC_GPIO),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configurar el timer MCPWM
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_FREQ_HZ * 32,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = spi_matrix[0][3],
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    // Configurar la fuente de sincronización GPIO
    mcpwm_gpio_sync_src_config_t gpio_sync_config = {
        .group_id = 0,
        .gpio_num = SYNC_GPIO,
        .flags.active_neg = 1,
        .flags.io_loop_back = 0,
        .flags.pull_down = 1,
        .flags.pull_up = 0,
    };

    mcpwm_sync_handle_t gpio_sync = NULL;
    ESP_ERROR_CHECK(mcpwm_new_gpio_sync_src(&gpio_sync_config, &gpio_sync));

    // Configurar la fase de sincronización
    mcpwm_timer_sync_phase_config_t sync_phase = {
        .sync_src = gpio_sync, .count_value = 0, .direction = MCPWM_TIMER_DIRECTION_UP};
    ESP_ERROR_CHECK(mcpwm_timer_set_phase_on_sync(timer, &sync_phase));

    // Configurar el operador MCPWM
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    // Configurar el comparador
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    // Configurar el generador
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = MCPWM_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    // Configurar las acciones del generador
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
        generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
        generator, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_HIGH)));

    // Configurar el valor de comparación
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, (uint32_t)(spi_matrix[0][4])));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(generator, -1, true));
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));

    ESP_LOGI(TAG, "MCPWM trigger initialized");
}

esp_err_t init_pulse_counter(void)
{
    // Configuración básica del contador de pulsos
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Configuración del canal
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = SINGLE_INPUT_PIN,
        .level_gpio_num = -1, // No usado
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Configurar el filtro de glitches
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Configurar para contar en flanco positivo inicialmente
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan,
                                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE, // Acción en flanco positivo
                                                 PCNT_CHANNEL_EDGE_ACTION_HOLD // Acción en flanco negativo
                                                 ));

    // Habilitar el contador
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));

    ESP_LOGI(TAG, "Pulse counter initialized");
    return ESP_OK;
}
#else // Funcionalidad para ADC interno

void start_adc_sampling(void)
{
    ESP_LOGI(TAG, "Starting ADC sampling");

    // Configurar el patrón de muestreo del ADC
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12, .channel = ADC_CHANNEL, .bit_width = ADC_BITWIDTH};

    // Configuración del manejador de ADC continuo
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE * 2,
        .conv_frame_size = 128,
        .flags.flush_pool = false,
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    // Configuración de ADC continuo
    adc_continuous_config_t continuous_config = {.pattern_num = 1,
                                                 .adc_pattern = &adc_pattern,
                                                 .sample_freq_hz = SAMPLE_RATE_HZ,
                                                 .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                                 .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1};

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &continuous_config));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    ESP_LOGI(TAG, "ADC sampling started at frequency: %d Hz", SAMPLE_RATE_HZ);
}

void stop_adc_sampling(void)
{
    ESP_LOGI(TAG, "Stopping ADC sampling");
    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}

void config_adc_sampling(void)
{
    ESP_LOGI(TAG, "Reconfiguring ADC with new frequency: %d Hz", SAMPLE_RATE_HZ / adc_divider);

    // Detener y desinicializar el ADC
    stop_adc_sampling();
    ESP_LOGI(TAG, "Stopped ADC");

    // Configuración del manejador de ADC continuo
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE * 2,
        .conv_frame_size = 128,
        .flags.flush_pool = false,
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    // Configurar el patrón de muestreo del ADC
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12, .channel = ADC_CHANNEL, .bit_width = ADC_BITWIDTH};

    // Configuración de ADC continuo con frecuencia ajustada
    adc_continuous_config_t continuous_config = {.pattern_num = 1,
                                                 .adc_pattern = &adc_pattern,
                                                 .sample_freq_hz = SAMPLE_RATE_HZ / adc_divider,
                                                 .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                                 .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1};

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &continuous_config));
    ESP_LOGI(TAG, "Configured ADC");

    // Actualizar el tiempo de espera para la conversión
    wait_convertion_time = WAIT_ADC_CONV_TIME * adc_divider;

    // Iniciar nuevamente el ADC
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    ESP_LOGI(TAG, "Started ADC");
}
#endif

void configure_gpio(void)
{
    // Configuración del pin de entrada para detección de trigger
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, // Sin interrupciones
        .mode = GPIO_MODE_INPUT, // Configurar como entrada
        .pin_bit_mask = (1ULL << SINGLE_INPUT_PIN), // Seleccionar el pin
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Habilitar pull-down
        .pull_up_en = GPIO_PULLUP_DISABLE, // Deshabilitar pull-up
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO %d configured as input for trigger detection", SINGLE_INPUT_PIN);
}

void my_timer_init(void)
{
    // Configuración del timer
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_DOWN,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS, // Deshabilitar la alarma
        .auto_reload = TIMER_AUTORELOAD_DIS,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // Calcular el tiempo de espera en microsegundos
    double sampling_frequency = get_sampling_frequency();
    wait_time_us = (BUF_SIZE / sampling_frequency) * 1000000;

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);

    ESP_LOGI(TAG, "Timer initialized with wait time: %llu us", wait_time_us);
}

void timer_wait(void)
{
    // Iniciar el temporizador
    timer_start(TIMER_GROUP_0, TIMER_0);

    // Esperar hasta que el temporizador alcance el valor de alarma (0)
    uint64_t timer_val;
    while (1) {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
        if (timer_val == 0) {
            break;
        }
    }

    // Pausar el temporizador
    timer_pause(TIMER_GROUP_0, TIMER_0);

    // Reiniciar el temporizador
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);
}

void init_square_wave(void)
{
    // Configurar el timer LEDC
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_10_BIT,
                                      .timer_num = SQUARE_WAVE_TIMER,
                                      .freq_hz = SQUARE_WAVE_FREQ,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Configurar el canal LEDC para la onda cuadrada
    ledc_channel_config_t channel_conf = {.gpio_num = SQUARE_WAVE_GPIO,
                                          .speed_mode = LEDC_LOW_SPEED_MODE,
                                          .channel = SQUARE_WAVE_CHANNEL,
                                          .timer_sel = SQUARE_WAVE_TIMER,
                                          .duty = 512, // 50% duty cycle (1024/2)
                                          .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    ESP_LOGI(TAG, "Square wave generator initialized at %d Hz", SQUARE_WAVE_FREQ);
}

void init_trigger_pwm(void)
{
    // Configurar el timer LEDC para el nivel de trigger
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = TRIGGER_PWM_RES,
        .freq_hz = TRIGGER_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = TRIGGER_PWM_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configurar el canal LEDC para el nivel de trigger
    ledc_channel.channel = TRIGGER_PWM_CHANNEL;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = TRIGGER_PWM_GPIO;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = TRIGGER_PWM_TIMER;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Trigger PWM initialized with frequency: %d Hz", TRIGGER_PWM_FREQ);
}

void init_sine_wave(void)
{
    // Configuración de la onda senoidal DAC
    dac_cosine_handle_t chan0_handle;
    dac_cosine_config_t cos0_cfg = {
        .chan_id = DAC_CHAN_1,
        .freq_hz = 10000, // Frecuencia de la señal senoidal en Hz
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos0_cfg, &chan0_handle));
    ESP_ERROR_CHECK(dac_cosine_start(chan0_handle));

    ESP_LOGI(TAG, "Sine wave generator initialized at 10 kHz");
}

void dac_sine_wave_task(void *pvParameters)
{
    init_sine_wave();
    vTaskDelete(NULL); // Finalizar la tarea una vez que la señal está configurada
}

esp_err_t set_trigger_level(int percentage)
{
    if (percentage < 0 || percentage > 100) {
        ESP_LOGE(TAG, "Invalid trigger level percentage: %d", percentage);
        return ESP_FAIL;
    }

    // Convertir porcentaje a ciclo de trabajo (0-1023)
    uint32_t duty = (percentage * (1 << TRIGGER_PWM_RES)) / 100;
    ESP_LOGI(TAG, "Setting trigger level to %d%% (duty: %lu)", percentage, duty);

    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, TRIGGER_PWM_CHANNEL, duty) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PWM duty cycle");
        return ESP_FAIL;
    }

    if (ledc_update_duty(LEDC_LOW_SPEED_MODE, TRIGGER_PWM_CHANNEL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update PWM duty cycle");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Funciones de información de configuración
double get_sampling_frequency(void)
{
#ifdef USE_EXTERNAL_ADC
    return 2500000; // Frecuencia de muestreo para ADC externo
#else
    return 494753; // Frecuencia de muestreo para ADC interno
#endif
}

int dividing_factor(void)
{
#ifdef USE_EXTERNAL_ADC
    return 1;
#else
    return 2;
#endif
}

int get_bits_per_packet(void)
{
    return 16; // Tamaño de paquete en bits
}

int get_data_mask(void)
{
#ifdef USE_EXTERNAL_ADC
    return 0x1FF8; // Máscara para los bits de datos (ADC externo)
#else
    return 0x0FFF; // Máscara para los bits de datos (ADC interno)
#endif
}

int get_channel_mask(void)
{
#ifdef USE_EXTERNAL_ADC
    return 0x0; // Máscara para los bits de canal (ADC externo)
#else
    return 0xF000; // Máscara para los bits de canal (ADC interno)
#endif
}

int get_useful_bits(void)
{
#ifdef USE_EXTERNAL_ADC
    return 10; // Resolución del ADC externo configurada
#else
    return ADC_BITWIDTH; // Resolución del ADC interno configurada
#endif
}

int get_discard_head(void)
{
#ifdef USE_EXTERNAL_ADC
    return 0;
#else
    return 0;
#endif
}

int get_discard_trailer(void)
{
    return 0;
}

int get_samples_per_packet(void)
{
    int total_samples = BUF_SIZE; // Número de muestras por llamada a send
    return total_samples - get_discard_head() - get_discard_trailer();
}

int get_max_bits(void)
{
#ifdef USE_EXTERNAL_ADC
    return 1023;
#else
    return 1023;
#endif
}

int get_mid_bits(void)
{
    // get_mid_bits debe ser siempre mayor que la mitad de get_max_bits
#ifdef USE_EXTERNAL_ADC
    return 512;
#else
    return 512;
#endif
}