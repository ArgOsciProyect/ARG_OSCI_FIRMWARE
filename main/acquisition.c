/**
 * @file acquisition.c
 * @brief Implementation of data acquisition functionality
 */

#include "acquisition.h"
#include "globals.h"

static const char *TAG = "ACQUISITION";

// Definitions of global variables declared as extern in globals.h
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

#ifndef USE_EXTERNAL_ADC
atomic_bool adc_is_running = ATOMIC_VAR_INIT(false);
#endif

static const voltage_scale_t voltage_scales[] = {
    {400.0, "200V, -200V"}, {120.0, "60V, -60V"}, {24.0, "12V, -12V"}, {6.0, "3V, -3V"}, {1.0, "500mV, -500mV"}};

int get_voltage_scales_count(void)
{
    return sizeof(voltage_scales) / sizeof(voltage_scales[0]);
}

const voltage_scale_t *get_voltage_scales(void)
{
    return voltage_scales;
}

#ifdef USE_EXTERNAL_ADC
SemaphoreHandle_t spi_mutex = NULL;

void spi_master_init(void)
{
    esp_err_t ret;
    int freq;

    // Configure the MISO pin as input with pull-down
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << PIN_NUM_MISO),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_ENABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configure the SPI bus
    spi_bus_config_t buscfg = {.miso_io_num = PIN_NUM_MISO,
                               .mosi_io_num = -1, // MOSI not used
                               .sclk_io_num = PIN_NUM_CLK,
                               .quadwp_io_num = -1,
                               .quadhd_io_num = -1,
                               .max_transfer_sz = 2 * BUF_SIZE,
                               .flags =
                                   SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MISO | SPICOMMON_BUSFLAG_IOMUX_PINS,
                               .intr_flags = ESP_INTR_FLAG_IRAM};

    // Initialize the SPI bus
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 3);
    ESP_ERROR_CHECK(ret);

    // Configure the SPI device
    spi_device_interface_config_t devcfg = {.clock_speed_hz = spi_matrix[0][0], // Initial SPI clock speed
                                            .mode = 0, // SPI mode 0
                                            .spics_io_num = PIN_NUM_CS, // CS pin
                                            .queue_size = 7, // Transaction queue size
                                            .pre_cb = NULL, // No pre-transaction callback
                                            .post_cb = NULL, // No post-transaction callback
                                            .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
                                            .cs_ena_pretrans = spi_matrix[0][1],
                                            .input_delay_ns = spi_matrix[0][2]};

    // Add the SPI device to the bus
    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master initialized");
    spi_device_get_actual_freq(spi, &freq);
    ESP_LOGI(TAG, "Actual SPI frequency: %d Hz", freq);
}

void init_mcpwm_trigger(void)
{
    // Configure the sync pin as input
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << SYNC_GPIO),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configure the MCPWM timer
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_FREQ_HZ * 32,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = spi_matrix[0][3],
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    // Configure the GPIO sync source
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

    // Configure the sync phase
    mcpwm_timer_sync_phase_config_t sync_phase = {
        .sync_src = gpio_sync, .count_value = 0, .direction = MCPWM_TIMER_DIRECTION_UP};
    ESP_ERROR_CHECK(mcpwm_timer_set_phase_on_sync(timer, &sync_phase));

    // Configure the MCPWM operator
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    // Configure the comparator
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    // Configure the generator
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = MCPWM_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    // Configure generator actions
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
        generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
        generator, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_HIGH)));

    // Configure the compare value
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, (uint32_t)(spi_matrix[0][4])));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(generator, -1, true));
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));

    ESP_LOGI(TAG, "MCPWM trigger initialized");
}

esp_err_t init_pulse_counter(void)
{
    // Basic pulse counter configuration
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Channel configuration
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = SINGLE_INPUT_PIN,
        .level_gpio_num = -1, // Not used
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Configure glitch filter
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Configure to count on positive edge initially
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan,
                                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE, // Action on positive edge
                                                 PCNT_CHANNEL_EDGE_ACTION_HOLD // Action on negative edge
                                                 ));

    // Enable the counter
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));

    ESP_LOGI(TAG, "Pulse counter initialized");
    return ESP_OK;
}
#else // Functionality for internal ADC

atomic_bool adc_initializing = ATOMIC_VAR_INIT(false);

void start_adc_sampling(void)
{
    ESP_LOGI(TAG, "Starting ADC sampling");

    // If already initializing, don't try again
    if (atomic_exchange(&adc_initializing, true)) {
        ESP_LOGW(TAG, "ADC initialization already in progress");
        return;
    }

    // Check if ADC is already running
    if (atomic_load(&adc_is_running)) {
        ESP_LOGW(TAG, "ADC is already running, skipping initialization");
        atomic_store(&adc_initializing, false);
        return;
    }

    // Add a small delay to ensure system resources are properly released
    vTaskDelay(pdMS_TO_TICKS(50));

    // Configure ADC pattern
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12, .channel = ADC_CHANNEL, .bit_width = ADC_BITWIDTH};

    // Configure continuous ADC
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE * 2,
        .conv_frame_size = 128,
        .flags.flush_pool = false,
    };

    esp_err_t ret = adc_continuous_new_handle(&adc_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC handle: %s", esp_err_to_name(ret));
        atomic_store(&adc_initializing, false);
        return;
    }

    adc_continuous_config_t continuous_config = {.pattern_num = 1,
                                                 .adc_pattern = &adc_pattern,
                                                 .sample_freq_hz = SAMPLE_RATE_HZ / adc_divider,
                                                 .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                                 .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1};

    ret = adc_continuous_config(adc_handle, &continuous_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC: %s", esp_err_to_name(ret));
        adc_continuous_deinit(adc_handle);
        atomic_store(&adc_initializing, false);
        return;
    }

    ret = adc_continuous_start(adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC: %s", esp_err_to_name(ret));
        adc_continuous_deinit(adc_handle);
        atomic_store(&adc_initializing, false);
        return;
    }

    // Set ADC as running
    atomic_store(&adc_is_running, true);
    atomic_store(&adc_initializing, false);

    ESP_LOGI(TAG, "ADC sampling started at frequency: %d Hz", SAMPLE_RATE_HZ / adc_divider);
}

void stop_adc_sampling(void)
{
    ESP_LOGI(TAG, "Stopping ADC sampling");

    // If we're in the middle of initialization, don't try to stop
    if (atomic_load(&adc_initializing)) {
        ESP_LOGW(TAG, "ADC is currently initializing, can't stop now");
        return;
    }

    // Only attempt to stop if ADC is running
    if (atomic_load(&adc_is_running)) {
        esp_err_t ret;

        // First stop
        ret = adc_continuous_stop(adc_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop ADC: %s", esp_err_to_name(ret));
        }

        // Add a delay before deinitializing
        vTaskDelay(pdMS_TO_TICKS(20));

        // Then deinitialize
        ret = adc_continuous_deinit(adc_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize ADC: %s", esp_err_to_name(ret));
        }

        atomic_store(&adc_is_running, false);
    } else {
        ESP_LOGW(TAG, "ADC was not running, nothing to stop");
    }
}

void config_adc_sampling(void)
{
    ESP_LOGI(TAG, "Reconfiguring ADC with new frequency: %d Hz", SAMPLE_RATE_HZ / adc_divider);

    // Stop and deinitialize the ADC
    stop_adc_sampling();
    ESP_LOGI(TAG, "Stopped ADC");

    // Add delay to ensure memory is properly freed
    vTaskDelay(pdMS_TO_TICKS(100));

    // Try multiple times to allocate memory for the ADC handle
    esp_err_t ret = ESP_ERR_NO_MEM;
    int retry_count = 0;
    const int max_retries = 5;

    while (ret == ESP_ERR_NO_MEM && retry_count < max_retries) {
        // Configuration of the continuous ADC handle
        adc_continuous_handle_cfg_t adc_config = {
            .max_store_buf_size = BUF_SIZE * 2,
            .conv_frame_size = 128,
            .flags.flush_pool = true, // Set to true to force cleanup
        };

        ret = adc_continuous_new_handle(&adc_config, &adc_handle);

        if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGW(TAG, "Memory allocation failed, retrying... (%d/%d)", retry_count + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(100)); // Wait before retrying
            retry_count++;
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC handle after %d attempts: %s", retry_count, esp_err_to_name(ret));
        return;
    }

    // Configure the ADC sampling pattern
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12, .channel = ADC_CHANNEL, .bit_width = ADC_BITWIDTH};

    // Continuous ADC configuration with adjusted frequency
    adc_continuous_config_t continuous_config = {.pattern_num = 1,
                                                 .adc_pattern = &adc_pattern,
                                                 .sample_freq_hz = SAMPLE_RATE_HZ / adc_divider,
                                                 .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                                 .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1};

    ret = adc_continuous_config(adc_handle, &continuous_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC: %s", esp_err_to_name(ret));
        adc_continuous_deinit(adc_handle);
        return;
    }

    ESP_LOGI(TAG, "Configured ADC");

    // Update the wait time for conversion
    wait_convertion_time = WAIT_ADC_CONV_TIME * adc_divider;

    // Start the ADC again
    ret = adc_continuous_start(adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC: %s", esp_err_to_name(ret));
        adc_continuous_deinit(adc_handle);
        return;
    }

    // Set ADC as running
    atomic_store(&adc_is_running, true);

    ESP_LOGI(TAG, "Started ADC");
}
#endif

void configure_gpio(void)
{
    // Configuration of the input pin for trigger detection
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, // No interrupts
        .mode = GPIO_MODE_INPUT, // Configure as input
        .pin_bit_mask = (1ULL << SINGLE_INPUT_PIN), // Select the pin
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Enable pull-down
        .pull_up_en = GPIO_PULLUP_DISABLE, // Disable pull-up
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO %d configured as input for trigger detection", SINGLE_INPUT_PIN);
}

void my_timer_init(void)
{
    // Timer configuration
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_DOWN,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS, // Disable alarm
        .auto_reload = TIMER_AUTORELOAD_DIS,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // Calculate the wait time in microseconds
    double sampling_frequency = get_sampling_frequency();
    wait_time_us = (BUF_SIZE / sampling_frequency) * 1000000;

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);

    ESP_LOGI(TAG, "Timer initialized with wait time: %llu us", wait_time_us);
}

void timer_wait(void)
{
    // Start the timer
    timer_start(TIMER_GROUP_0, TIMER_0);

    // Wait until the timer reaches the alarm value (0)
    uint64_t timer_val;
    while (1) {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &timer_val);
        if (timer_val == 0) {
            break;
        }
    }

    // Pause the timer
    timer_pause(TIMER_GROUP_0, TIMER_0);

    // Reset the timer
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, wait_time_us);
}

void init_square_wave(void)
{
    // Configure the LEDC timer
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_10_BIT,
                                      .timer_num = SQUARE_WAVE_TIMER,
                                      .freq_hz = SQUARE_WAVE_FREQ,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Configure the LEDC channel for the square wave
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
    // Configure the LEDC timer for the trigger level
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = TRIGGER_PWM_RES,
        .freq_hz = TRIGGER_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = TRIGGER_PWM_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configure the LEDC channel for the trigger level
    ledc_channel.channel = TRIGGER_PWM_CHANNEL;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = TRIGGER_PWM_GPIO;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = TRIGGER_PWM_TIMER;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Trigger PWM initialized with frequency: %d Hz", TRIGGER_PWM_FREQ);

    esp_err_t ret = set_trigger_level(0); // Initialize the trigger level to 0%
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set trigger level");
    }
}

void init_sine_wave(void)
{
    // Sine wave configuration using DAC
    dac_cosine_handle_t chan0_handle;
    dac_cosine_config_t cos0_cfg = {
        .chan_id = DAC_CHAN_1,
        .freq_hz = 20000, // Sine wave frequency in Hz
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = true,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos0_cfg, &chan0_handle));
    ESP_ERROR_CHECK(dac_cosine_start(chan0_handle));

    ESP_LOGI(TAG, "Sine wave generator initialized at 20 kHz");
}

void dac_sine_wave_task(void *pvParameters)
{
    init_sine_wave();
    vTaskDelete(NULL); // End the task once the signal is configured
}

esp_err_t set_trigger_level(int percentage)
{
    if (percentage < 0 || percentage > 100) {
        ESP_LOGE(TAG, "Invalid trigger level percentage: %d", percentage);
        return ESP_FAIL;
    }

    // Convert percentage to duty cycle (0-1023)
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

// Configuration information functions
double get_sampling_frequency(void)
{
#ifdef USE_EXTERNAL_ADC
    return 2500000; // Sampling frequency for external ADC
#else
    return 496490; // Sampling frequency for internal ADC
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
    return 16; // Packet size in bits
}

int get_data_mask(void)
{
#ifdef USE_EXTERNAL_ADC
    return 0x1FF8; // Mask for data bits (external ADC)
#else
    return 0x0FFF; // Mask for data bits (internal ADC)
#endif
}

int get_channel_mask(void)
{
#ifdef USE_EXTERNAL_ADC
    return 0x0; // Mask for channel bits (external ADC)
#else
    return 0xF000; // Mask for channel bits (internal ADC)
#endif
}

int get_useful_bits(void)
{
#ifdef USE_EXTERNAL_ADC
    return 10; // Resolution of the configured external ADC
#else
    return ADC_BITWIDTH; // Resolution of the configured internal ADC
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
    int total_samples = BUF_SIZE; // Number of samples per send call
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
    // get_mid_bits must always be greater than half of get_max_bits
#ifdef USE_EXTERNAL_ADC
    return 512;
#else
    return 512;
#endif
}