#include "button_input.h"

#include "button_input_host.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static int event_count;
static button_input_event_t last_event;
static void *last_handler_argument;

static void event_handler(const button_input_event_t *event) {
    ++event_count;
    last_event = *event;
}

static const module_descriptor_t *descriptor(void) {
    const module_descriptor_t *value = button_input_module_descriptor();
    assert(value != NULL);
    assert(value->initialize != NULL);
    assert(value->shutdown != NULL);
    return value;
}

static void prepare(void) {
    descriptor()->shutdown();
    button_input_host_reset_state();
    button_input_host_reset();
    assert(button_input_init() == ESP_OK);
    assert(button_input_is_ready());
    assert(button_input_set_event_handler(event_handler) == ESP_OK);
    event_count = 0;
    memset(&last_event, 0, sizeof(last_event));
    last_handler_argument = NULL;
}

static void press_primary_at(TickType_t ticks) {
    button_input_host_set_tick_count(ticks);
    button_input_host_set_gpio_level(
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO,
        0
    );
    button_input_host_step();
}

static void release_primary_at(TickType_t ticks) {
    button_input_host_set_tick_count(ticks);
    button_input_host_set_gpio_level(
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO,
        1
    );
    button_input_host_step();
}

static void complete_short_press(TickType_t start_ticks) {
    press_primary_at(start_ticks);
    button_input_host_set_tick_count(start_ticks + 25);
    button_input_host_step();
    release_primary_at(start_ticks + 100);
    button_input_host_set_tick_count(start_ticks + 125);
    button_input_host_step();
}

static void test_calls_before_init_are_refused_and_names_are_stable(void) {
    button_input_host_reset_state();
    button_input_host_reset();

    assert(!button_input_is_ready());
    assert(button_input_set_event_handler(event_handler) ==
           ESP_ERR_INVALID_STATE);
    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(snapshot.button_count == 0);

    assert(strcmp(
        button_input_gesture_name(BUTTON_INPUT_GESTURE_NONE),
        "none"
    ) == 0);
    assert(strcmp(
        button_input_gesture_name(BUTTON_INPUT_GESTURE_SHORT_PRESS),
        "short_press"
    ) == 0);
    assert(strcmp(
        button_input_gesture_name(BUTTON_INPUT_GESTURE_LONG_PRESS),
        "long_press"
    ) == 0);
    assert(strcmp(
        button_input_gesture_name(BUTTON_INPUT_GESTURE_VERY_LONG_PRESS),
        "very_long_press"
    ) == 0);
    assert(strcmp(
        button_input_gesture_name(BUTTON_INPUT_GESTURE_DOUBLE_PRESS),
        "double_press"
    ) == 0);
    assert(strcmp(
        button_input_gesture_name((button_input_gesture_t)99),
        "none"
    ) == 0);

    assert(strcmp(button_input_error_name(BUTTON_INPUT_OK), "ok") == 0);
    assert(strcmp(
        button_input_error_name(BUTTON_INPUT_ERR_NOT_INITIALIZED),
        "button_input_not_initialized"
    ) == 0);
    assert(strcmp(
        button_input_error_name(BUTTON_INPUT_ERR_INVALID_ARGUMENT),
        "button_input_invalid_argument"
    ) == 0);
    assert(strcmp(
        button_input_error_name(BUTTON_INPUT_ERR_GPIO),
        "button_input_gpio_error"
    ) == 0);
    assert(strcmp(
        button_input_error_name(BUTTON_INPUT_ERR_NO_MEM),
        "button_input_no_memory"
    ) == 0);
    assert(strcmp(
        button_input_error_name((button_input_error_t)99),
        "button_input_internal"
    ) == 0);
}

static void test_descriptor_and_primary_secondary_profile(void) {
    prepare();

    assert(strcmp(descriptor()->module_name, "button_input") == 0);
    assert(strcmp(descriptor()->version, "1.0.0") == 0);
    assert(button_input_host_gpio_config_calls() == 2);
    assert(button_input_host_handler_add_calls() == 2);
    assert(button_input_host_task_create_calls() == 1);
    assert(button_input_host_isr_install_calls() == 1);
    assert(button_input_host_isr_service_installed());

    const gpio_config_t primary =
        button_input_host_gpio_config_at(0);
    const gpio_config_t secondary =
        button_input_host_gpio_config_at(1);
    assert(primary.pin_bit_mask ==
           (1ULL << CONFIG_BUTTON_INPUT_PRIMARY_GPIO));
    assert(secondary.pin_bit_mask ==
           (1ULL << CONFIG_BUTTON_INPUT_SECONDARY_GPIO));
    assert(primary.mode == GPIO_MODE_INPUT);
    assert(secondary.mode == GPIO_MODE_INPUT);
    assert(primary.pull_up_en == GPIO_PULLUP_ENABLE);
    assert(primary.pull_down_en == GPIO_PULLDOWN_DISABLE);
    assert(secondary.pull_up_en == GPIO_PULLUP_ENABLE);
    assert(secondary.pull_down_en == GPIO_PULLDOWN_DISABLE);
    assert(primary.intr_type == GPIO_INTR_DISABLE);
    assert(secondary.intr_type == GPIO_INTR_DISABLE);

    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(snapshot.button_count == 2);
    assert(!snapshot.is_pressed[0]);
    assert(!snapshot.is_pressed[1]);
    assert(snapshot.short_press_count[0] == 0);
    assert(snapshot.long_press_count[0] == 0);
    assert(snapshot.double_press_count[0] == 0);
    assert(snapshot.very_long_press_count[0] == 0);

    assert(button_input_init() == ESP_OK);
    assert(button_input_host_gpio_config_calls() == 2);
    assert(button_input_host_task_create_calls() == 1);
}

static void test_short_press_reports_a_bounded_event(void) {
    prepare();
    complete_short_press(1000);

    assert(event_count == 0);
    button_input_host_advance_ticks(CONFIG_BUTTON_INPUT_DOUBLE_PRESS_MS);
    button_input_host_step();

    assert(event_count == 1);
    assert(last_event.button_index == 0);
    assert(last_event.gesture == BUTTON_INPUT_GESTURE_SHORT_PRESS);
    assert(last_event.held_ms == 100);
    assert(last_event.timestamp_ms == 1475);

    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(!snapshot.is_pressed[0]);
    assert(snapshot.short_press_count[0] == 1);
    assert(snapshot.long_press_count[0] == 0);
    assert(snapshot.double_press_count[0] == 0);
    assert(snapshot.very_long_press_count[0] == 0);
}

static void test_double_press_uses_the_second_press_window(void) {
    prepare();
    complete_short_press(1000);

    press_primary_at(1200);
    button_input_host_set_tick_count(1225);
    button_input_host_step();
    release_primary_at(1285);
    button_input_host_set_tick_count(1310);
    button_input_host_step();

    assert(event_count == 1);
    assert(last_event.button_index == 0);
    assert(last_event.gesture == BUTTON_INPUT_GESTURE_DOUBLE_PRESS);
    assert(last_event.held_ms == 85);
    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(snapshot.short_press_count[0] == 0);
    assert(snapshot.double_press_count[0] == 1);
}

static void test_long_press_is_emitted_once_and_clears_short_pending(void) {
    prepare();
    press_primary_at(1000);
    button_input_host_set_tick_count(1025);
    button_input_host_step();
    button_input_host_set_tick_count(
        1025 + CONFIG_BUTTON_INPUT_LONG_PRESS_MS
    );
    button_input_host_step();

    assert(event_count == 1);
    assert(last_event.gesture == BUTTON_INPUT_GESTURE_LONG_PRESS);
    assert(last_event.held_ms == CONFIG_BUTTON_INPUT_LONG_PRESS_MS);

    release_primary_at(2300);
    button_input_host_set_tick_count(2325);
    button_input_host_step();
    assert(event_count == 1);

    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(snapshot.short_press_count[0] == 0);
    assert(snapshot.long_press_count[0] == 1);
    assert(snapshot.double_press_count[0] == 0);
    assert(snapshot.very_long_press_count[0] == 0);
}

static void test_very_long_press_wins_when_thresholds_are_crossed_together(void) {
    prepare();
    press_primary_at(1000);
    button_input_host_set_tick_count(1025);
    button_input_host_step();
    button_input_host_set_tick_count(
        1025 + CONFIG_BUTTON_INPUT_VERY_LONG_PRESS_MS
    );
    button_input_host_step();
    button_input_host_step();

    assert(event_count == 1);
    assert(last_event.gesture == BUTTON_INPUT_GESTURE_VERY_LONG_PRESS);
    assert(last_event.held_ms == CONFIG_BUTTON_INPUT_VERY_LONG_PRESS_MS);

    release_primary_at(6100);
    button_input_host_set_tick_count(6125);
    button_input_host_step();
    assert(event_count == 1);

    const button_input_snapshot_t snapshot = button_input_get_snapshot();
    assert(snapshot.short_press_count[0] == 0);
    assert(snapshot.long_press_count[0] == 0);
    assert(snapshot.double_press_count[0] == 0);
    assert(snapshot.very_long_press_count[0] == 1);
}

static void test_press_bounce_is_discarded_and_release_bounce_is_absorbed(void) {
    prepare();
    press_primary_at(1000);
    button_input_host_set_tick_count(1010);
    button_input_host_set_gpio_level(
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO,
        1
    );
    button_input_host_step();
    button_input_host_set_tick_count(1080);
    button_input_host_step();
    assert(event_count == 0);

    press_primary_at(2000);
    button_input_host_set_tick_count(2025);
    button_input_host_step();
    release_primary_at(2100);
    button_input_host_set_tick_count(2110);
    button_input_host_set_gpio_level(
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO,
        0
    );
    button_input_host_step();
    button_input_host_set_tick_count(2135);
    button_input_host_set_gpio_level(
        (gpio_num_t)CONFIG_BUTTON_INPUT_PRIMARY_GPIO,
        1
    );
    button_input_host_step();
    button_input_host_set_tick_count(2145);
    button_input_host_step();
    assert(event_count == 0);

    button_input_host_set_tick_count(2160);
    button_input_host_step();
    assert(event_count == 0);
    button_input_host_advance_ticks(CONFIG_BUTTON_INPUT_DOUBLE_PRESS_MS);
    button_input_host_step();
    assert(event_count == 1);
    assert(last_event.gesture == BUTTON_INPUT_GESTURE_SHORT_PRESS);
}

static void test_input_failures_roll_back_partial_resources(void) {
    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_set_create_mutex_result(0);
    assert(button_input_init() == ESP_ERR_NO_MEM);
    assert(!button_input_is_ready());
    assert(button_input_host_gpio_config_calls() == 0);
    assert(button_input_host_task_create_calls() == 0);

    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_set_create_binary_result(0);
    assert(button_input_init() == ESP_ERR_NO_MEM);
    assert(!button_input_is_ready());
    assert(button_input_host_semaphore_delete_calls() == 1);
    assert(button_input_host_gpio_config_calls() == 0);

    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_set_isr_service_result(ESP_ERR_INVALID_ARG);
    assert(button_input_init() == ESP_ERR_INVALID_ARG);
    assert(!button_input_is_ready());
    assert(button_input_host_gpio_config_calls() == 0);
    assert(button_input_host_handler_add_calls() == 0);

    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_fail_gpio_config_at(2);
    assert(button_input_init() == ESP_ERR_INVALID_ARG);
    assert(!button_input_is_ready());
    assert(button_input_host_gpio_config_calls() == 2);
    assert(button_input_host_gpio_reset_calls() == 1);
    assert(button_input_host_isr_uninstall_calls() == 1);
    assert(!button_input_host_isr_service_installed());

    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_set_task_create_result(0);
    assert(button_input_init() == ESP_ERR_NO_MEM);
    assert(!button_input_is_ready());
    assert(button_input_host_gpio_config_calls() == 2);
    assert(button_input_host_gpio_reset_calls() == 2);
    assert(button_input_host_isr_uninstall_calls() == 1);

    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_fail_handler_add_at(2);
    assert(button_input_init() == ESP_ERR_INVALID_ARG);
    assert(!button_input_is_ready());
    assert(button_input_host_handler_add_calls() == 2);
    assert(button_input_host_handler_remove_calls() == 1);
    assert(button_input_host_intr_disable_calls() == 1);
    assert(button_input_host_gpio_reset_calls() == 2);
    assert(button_input_host_isr_uninstall_calls() == 1);
}

static void test_shared_isr_service_is_not_owned_or_uninstalled(void) {
    prepare();
    button_input_host_reset_state();
    button_input_host_reset();
    button_input_host_set_isr_service_result(ESP_ERR_INVALID_STATE);
    assert(button_input_init() == ESP_OK);
    assert(button_input_is_ready());
    assert(!button_input_host_isr_service_installed());

    descriptor()->shutdown();
    assert(!button_input_is_ready());
    assert(button_input_host_isr_uninstall_calls() == 0);
}

static void test_shutdown_and_reinitialize_are_safe(void) {
    prepare();
    descriptor()->shutdown();
    assert(!button_input_is_ready());
    assert(button_input_host_handler_remove_calls() == 2);
    assert(button_input_host_gpio_reset_calls() == 2);
    assert(button_input_host_isr_uninstall_calls() == 1);
    descriptor()->shutdown();
    assert(!button_input_is_ready());

    button_input_host_reset_state();
    button_input_host_reset();
    assert(button_input_init() == ESP_OK);
    assert(button_input_is_ready());
    descriptor()->shutdown();
}

int main(void) {
    test_calls_before_init_are_refused_and_names_are_stable();
    test_descriptor_and_primary_secondary_profile();
    test_short_press_reports_a_bounded_event();
    test_double_press_uses_the_second_press_window();
    test_long_press_is_emitted_once_and_clears_short_pending();
    test_very_long_press_wins_when_thresholds_are_crossed_together();
    test_press_bounce_is_discarded_and_release_bounce_is_absorbed();
    test_input_failures_roll_back_partial_resources();
    test_shared_isr_service_is_not_owned_or_uninstalled();
    test_shutdown_and_reinitialize_are_safe();
    return 0;
}
