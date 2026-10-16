#include "led_indicator.h"

#include "driver/ledc.h"
#include "esp_err.h"

#include <assert.h>
#include <string.h>

void led_indicator_host_reset(void);
void led_indicator_host_set_gpio_valid(bool valid);
void led_indicator_host_set_create_mutex_fails(bool fails);
void led_indicator_host_set_create_binary_fails(bool fails);
void led_indicator_host_set_create_task_fails(bool fails);
void led_indicator_host_set_timer_config_result(esp_err_t result);
void led_indicator_host_set_channel_config_result(esp_err_t result);
void led_indicator_host_set_set_duty_result(esp_err_t result);
uint32_t led_indicator_host_last_duty(void);
ledc_timer_config_t led_indicator_host_last_timer_config(void);
ledc_channel_config_t led_indicator_host_last_channel_config(void);
int led_indicator_host_timer_config_calls(void);
int led_indicator_host_channel_config_calls(void);
int led_indicator_host_set_duty_calls(void);
int led_indicator_host_stop_calls(void);
int led_indicator_host_semaphore_delete_calls(void);
int led_indicator_host_task_create_calls(void);

/* The module keeps process-wide state across cases, so each case starts by
   shutting down whatever the previous one left behind and then resetting the
   peripheral stand-ins. Shutdown on a clean module is a no-op. */
static void prepare(void) {
    const module_descriptor_t *descriptor = led_indicator_module_descriptor();
    assert(descriptor != NULL);
    assert(descriptor->shutdown != NULL);
    descriptor->shutdown();
    led_indicator_host_reset();
}

static void test_calls_before_init_are_refused(void) {
    assert(!led_indicator_is_ready());
    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_IDLE) ==
        LED_INDICATOR_ERR_NOT_INITIALIZED
    );
    assert(led_indicator_set_enabled(true) == ESP_ERR_INVALID_STATE);
    /* With no mutex yet the snapshot still reports the boot-time defaults. */
    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(!snapshot.is_ready);
    assert(!snapshot.is_enabled);
    assert(snapshot.state == LED_INDICATOR_STATE_OFF);
    assert(snapshot.transitions == 0);
}

static void test_invalid_gpio_is_refused_before_touching_ledc(void) {
    prepare();
    led_indicator_host_set_gpio_valid(false);
    assert(led_indicator_init() == ESP_ERR_INVALID_ARG);
    assert(!led_indicator_is_ready());
    assert(led_indicator_host_timer_config_calls() == 0);
    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(snapshot.last_error == LED_INDICATOR_ERR_GPIO_UNAVAILABLE);
}

static void test_semaphore_failure_releases_created_objects(void) {
    prepare();
    led_indicator_host_set_create_binary_fails(true);
    assert(led_indicator_init() == ESP_ERR_NO_MEM);
    assert(!led_indicator_is_ready());
    /* The mutex created before the failure must not leak. */
    assert(led_indicator_host_semaphore_delete_calls() > 0);
    assert(led_indicator_host_timer_config_calls() == 0);
}

static void test_timer_config_failure_is_reported(void) {
    prepare();
    led_indicator_host_set_timer_config_result(ESP_ERR_INVALID_ARG);
    assert(led_indicator_init() == ESP_ERR_INVALID_ARG);
    assert(!led_indicator_is_ready());
    assert(led_indicator_host_channel_config_calls() == 0);
    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(snapshot.last_error == LED_INDICATOR_ERR_GPIO_UNAVAILABLE);
}

static void test_channel_config_failure_deconfigures_ledc(void) {
    prepare();
    led_indicator_host_set_channel_config_result(ESP_ERR_INVALID_ARG);
    assert(led_indicator_init() == ESP_ERR_INVALID_ARG);
    assert(!led_indicator_is_ready());
    assert(led_indicator_host_stop_calls() == 1);
}

static void test_task_creation_failure_deconfigures_ledc(void) {
    prepare();
    led_indicator_host_set_create_task_fails(true);
    assert(led_indicator_init() == ESP_ERR_NO_MEM);
    assert(!led_indicator_is_ready());
    assert(led_indicator_host_stop_calls() == 1);
    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(snapshot.last_error == LED_INDICATOR_ERR_NO_MEM);
}

static void test_successful_init_uses_the_contract_profile(void) {
    prepare();
    assert(led_indicator_init() == ESP_OK);
    assert(led_indicator_is_ready());

    /* Idempotent init must not configure LEDC or create a second task. */
    assert(led_indicator_init() == ESP_OK);
    assert(led_indicator_host_timer_config_calls() == 1);
    assert(led_indicator_host_channel_config_calls() == 1);
    assert(led_indicator_host_task_create_calls() == 1);

    const ledc_timer_config_t timer = led_indicator_host_last_timer_config();
    assert(timer.freq_hz == CONFIG_LED_INDICATOR_PWM_FREQUENCY_HZ);
    assert(timer.duty_resolution == LEDC_TIMER_10_BIT);
    assert(!timer.deconfigure);

    const ledc_channel_config_t channel =
        led_indicator_host_last_channel_config();
    assert(channel.gpio_num == CONFIG_LED_INDICATOR_GPIO);
    assert(channel.duty == 0);
    assert(!channel.deconfigure);
    assert(
        channel.flags.output_invert ==
        (CONFIG_LED_INDICATOR_ACTIVE_LOW ? 1U : 0U)
    );

    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(snapshot.is_ready);
    assert(snapshot.is_enabled);
    assert(snapshot.state == LED_INDICATOR_STATE_BOOTING);
    assert(snapshot.transitions == 0);
    assert(snapshot.last_error == LED_INDICATOR_OK);
}

static void test_set_state_validates_and_counts_transitions(void) {
    prepare();
    assert(led_indicator_init() == ESP_OK);

    /* An out-of-contract state is refused and must not change anything. */
    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_COUNT) ==
        LED_INDICATOR_ERR_INVALID_STATE
    );
    assert(
        led_indicator_set_state((led_indicator_state_t)-1) ==
        LED_INDICATOR_ERR_INVALID_STATE
    );
    assert(led_indicator_get_state() == LED_INDICATOR_STATE_BOOTING);
    assert(led_indicator_get_snapshot().transitions == 0);

    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_LISTENING) ==
        LED_INDICATOR_OK
    );
    assert(led_indicator_get_state() == LED_INDICATOR_STATE_LISTENING);
    assert(led_indicator_get_snapshot().transitions == 1);

    /* Repeating the same state is harmless and must not count again. */
    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_LISTENING) ==
        LED_INDICATOR_OK
    );
    assert(led_indicator_get_snapshot().transitions == 1);

    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_THINKING) ==
        LED_INDICATOR_OK
    );
    assert(led_indicator_get_snapshot().transitions == 2);
}

static void test_disable_forces_the_output_off(void) {
    prepare();
    assert(led_indicator_init() == ESP_OK);
    assert(
        led_indicator_set_state(LED_INDICATOR_STATE_IDLE) == LED_INDICATOR_OK
    );

    const int set_duty_before = led_indicator_host_set_duty_calls();
    assert(led_indicator_set_enabled(false) == ESP_OK);
    assert(led_indicator_host_set_duty_calls() == set_duty_before + 1);
    assert(led_indicator_host_last_duty() == 0);
    const led_indicator_snapshot_t snapshot = led_indicator_get_snapshot();
    assert(!snapshot.is_enabled);
    /* Disabling does not discard the selected state. */
    assert(snapshot.state == LED_INDICATOR_STATE_IDLE);

    assert(led_indicator_set_enabled(true) == ESP_OK);
    assert(led_indicator_get_snapshot().is_enabled);
}

static void test_disable_failure_keeps_the_guardian_state(void) {
    prepare();
    assert(led_indicator_init() == ESP_OK);

    led_indicator_host_set_set_duty_result(ESP_ERR_INVALID_STATE);
    assert(led_indicator_set_enabled(false) == ESP_ERR_INVALID_STATE);
    /* The caller must be able to tell the LED may still be lit. */
    assert(led_indicator_get_snapshot().is_enabled);
    assert(
        led_indicator_get_snapshot().last_error ==
        LED_INDICATOR_ERR_GPIO_UNAVAILABLE
    );
}

static void test_state_names_are_stable(void) {
    assert(
        strcmp(led_indicator_state_name(LED_INDICATOR_STATE_OFF), "off") == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_BOOTING),
            "booting"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_PROVISIONING),
            "provisioning"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_WIFI_CONNECTING),
            "wifi_connecting"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_CONNECTING),
            "connecting"
        ) == 0
    );
    assert(
        strcmp(led_indicator_state_name(LED_INDICATOR_STATE_IDLE), "idle") == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_LISTENING),
            "listening"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_THINKING),
            "thinking"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_SPEAKING),
            "speaking"
        ) == 0
    );
    assert(
        strcmp(led_indicator_state_name(LED_INDICATOR_STATE_MUTED), "muted") == 0
    );
    assert(
        strcmp(led_indicator_state_name(LED_INDICATOR_STATE_ERROR), "error") == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_LOW_BATTERY),
            "low_battery"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_FACTORY_RESET),
            "factory_reset"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_state_name(LED_INDICATOR_STATE_OTA_UPGRADING),
            "ota_upgrading"
        ) == 0
    );
    /* An out-of-contract state falls back to "off". */
    assert(
        strcmp(led_indicator_state_name(LED_INDICATOR_STATE_COUNT), "off") == 0
    );
}

static void test_error_names_are_stable(void) {
    assert(strcmp(led_indicator_error_name(LED_INDICATOR_OK), "ok") == 0);
    assert(
        strcmp(
            led_indicator_error_name(LED_INDICATOR_ERR_NOT_INITIALIZED),
            "not_initialized"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_error_name(LED_INDICATOR_ERR_INVALID_STATE),
            "invalid_state"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_error_name(LED_INDICATOR_ERR_GPIO_UNAVAILABLE),
            "gpio_unavailable"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_error_name(LED_INDICATOR_ERR_NO_MEM),
            "no_memory"
        ) == 0
    );
    assert(
        strcmp(
            led_indicator_error_name((led_indicator_error_t)99),
            "gpio_unavailable"
        ) == 0
    );
}

static void test_shutdown_and_reinit(void) {
    const module_descriptor_t *descriptor = led_indicator_module_descriptor();
    assert(descriptor != NULL);
    assert(strcmp(descriptor->module_name, "led_indicator") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != NULL);
    assert(descriptor->shutdown != NULL);

    prepare();
    assert(led_indicator_init() == ESP_OK);
    descriptor->shutdown();
    assert(!led_indicator_is_ready());
    assert(led_indicator_host_stop_calls() == 1);
    assert(led_indicator_host_semaphore_delete_calls() >= 3);

    led_indicator_host_reset();
    /* The module can be re-initialised after a clean shutdown. */
    assert(led_indicator_init() == ESP_OK);
    assert(led_indicator_is_ready());
    descriptor->shutdown();
}

int main(void) {
    /* The module has a shutdown, so the pre-init assertions run first. */
    test_calls_before_init_are_refused();
    test_invalid_gpio_is_refused_before_touching_ledc();
    test_semaphore_failure_releases_created_objects();
    test_timer_config_failure_is_reported();
    test_channel_config_failure_deconfigures_ledc();
    test_task_creation_failure_deconfigures_ledc();
    test_successful_init_uses_the_contract_profile();
    test_set_state_validates_and_counts_transitions();
    test_disable_forces_the_output_off();
    test_disable_failure_keeps_the_guardian_state();
    test_state_names_are_stable();
    test_error_names_are_stable();
    test_shutdown_and_reinit();
    return 0;
}
