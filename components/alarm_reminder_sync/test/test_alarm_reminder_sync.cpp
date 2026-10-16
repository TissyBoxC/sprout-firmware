#include "alarm_reminder_sync.h"

#include <cassert>
#include <cstring>

#include "alarm_reminder.h"
#include "alarm_reminder_sync_host.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

alarm_reminder_t make_alarm(
    const char *alarm_id,
    const char *label,
    uint16_t minute_of_day,
    uint8_t weekday_mask,
    uint8_t snooze_minutes
) {
    alarm_reminder_t alarm = {};
    strncpy(alarm.alarm_id, alarm_id, ALARM_REMINDER_ID_SIZE - 1);
    strncpy(alarm.child_id, "child-1", ALARM_REMINDER_CHILD_ID_SIZE - 1);
    strncpy(alarm.label, label, ALARM_REMINDER_LABEL_SIZE - 1);
    strncpy(alarm.prompt, "Time to wake up", ALARM_REMINDER_PROMPT_SIZE - 1);
    alarm.minute_of_day = minute_of_day;
    alarm.weekday_mask = weekday_mask;
    alarm.snooze_minutes = snooze_minutes;
    alarm.enabled = true;
    return alarm;
}

void reset_all() {
    alarm_reminder_sync_host_reset();
    alarm_reminder_sync_host_reset_module();
    assert(alarm_reminder_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    alarm_reminder_sync_host_set_http_sequence(statuses, bodies, 1);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        alarm_reminder_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "alarm_reminder_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_alarm_store() {
    alarm_reminder_sync_host_reset();
    alarm_reminder_sync_host_reset_module();

    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();

    alarm_reminder_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_IDLE
    );
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);

    alarm_reminder_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    alarm_reminder_sync_host_set_time_synchronized(false);
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);

    alarm_reminder_sync_host_set_time_synchronized(true);
    alarm_reminder_sync_host_set_registered(false);
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    alarm_reminder_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);

    reset_all();
    alarm_reminder_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);

    reset_all();
    alarm_reminder_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_request_count() == 0);
}

void test_successful_fetch_replaces_schedule() {
    reset_all();
    const char *body =
        "{\"data\":{\"alarms\":["
        "{\"id\":\"alarm-morning\",\"child_id\":\"child-1\","
        "\"label\":\"Wake up\",\"prompt\":\"Good morning\","
        "\"alarm_time\":\"07:05\",\"weekdays\":[1,3,5],"
        "\"snooze_minutes\":0},"
        "{\"id\":\"alarm-night\",\"child_id\":\"child-1\","
        "\"label\":\"Bedtime\",\"alarm_time\":\"21:30\","
        "\"weekdays\":[9],\"snooze_minutes\":45},"
        "{\"id\":\"invalid\",\"label\":\"\",\"alarm_time\":\"25:00\"}"
        "]}}";
    set_single_response(200, body);

    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_sync_host_request_count() == 1);
    assert(
        strcmp(
            alarm_reminder_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "alarm-reminders"
        ) == 0
    );
    assert(
        strcmp(
            alarm_reminder_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        alarm_reminder_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(alarm_reminder_count() == 2);

    alarm_reminder_t alarm = {};
    assert(alarm_reminder_get("alarm-morning", &alarm));
    assert(alarm.minute_of_day == 7 * 60 + 5);
    assert(alarm.weekday_mask == 0x15);
    assert(alarm.snooze_minutes == ALARM_REMINDER_MIN_SNOOZE_MINUTES);
    assert(alarm.enabled);

    assert(alarm_reminder_get("alarm-night", &alarm));
    assert(alarm.minute_of_day == 21 * 60 + 30);
    assert(alarm.weekday_mask == 0x7F);
    assert(alarm.snooze_minutes == ALARM_REMINDER_MAX_SNOOZE_MINUTES);
}

void test_empty_snapshot_clears_schedule() {
    reset_all();
    const alarm_reminder_t local = make_alarm(
        "alarm-local",
        "Local",
        8 * 60,
        0x7F,
        5
    );
    assert(alarm_reminder_upsert(&local) == ESP_OK);
    assert(alarm_reminder_count() == 1);
    set_single_response(200, "{\"data\":{\"alarms\":[]}}");

    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_count() == 0);
}

void test_invalid_responses_leave_cache_untouched() {
    reset_all();
    const alarm_reminder_t local = make_alarm(
        "alarm-local",
        "Local",
        8 * 60,
        0x7F,
        5
    );
    assert(alarm_reminder_upsert(&local) == ESP_OK);

    set_single_response(200, "{not-json");
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_ARG);
    assert(alarm_reminder_count() == 1);

    reset_all();
    assert(alarm_reminder_upsert(&local) == ESP_OK);
    set_single_response(200, "{\"data\":{\"alarms\":[{\"id\":\"\"}]}}");
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_ARG);
    assert(alarm_reminder_count() == 1);

    reset_all();
    assert(alarm_reminder_upsert(&local) == ESP_OK);
    set_single_response(500, "{\"data\":{\"alarms\":[]}}");
    assert(alarm_reminder_sync_now() == ESP_FAIL);
    assert(alarm_reminder_count() == 1);
}

void test_unauthorized_response_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);
    assert(alarm_reminder_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    alarm_reminder_sync_host_set_client_init_fails(true);
    assert(alarm_reminder_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    alarm_reminder_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    alarm_reminder_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(alarm_reminder_sync_now() == ESP_ERR_INVALID_ARG);
    assert(alarm_reminder_sync_host_request_count() == 0);
}

void test_events_are_reported_and_acknowledged() {
    reset_all();
    const alarm_reminder_t local = make_alarm(
        "alarm-morning",
        "Wake up",
        7 * 60,
        0x7F,
        5
    );
    assert(alarm_reminder_upsert(&local) == ESP_OK);
    assert(alarm_reminder_mark_ringing("alarm-morning", 20261015) == ESP_OK);
    assert(alarm_reminder_dismiss("alarm-morning", 20261015) == ESP_OK);
    assert(alarm_reminder_pending_event_count() == 1);

    const int statuses[] = {200, 200};
    const char *bodies[] = {
        "{\"data\":{\"alarms\":[]}}",
        "{}",
    };
    alarm_reminder_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_sync_host_request_count() == 2);
    assert(
        alarm_reminder_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        strcmp(
            alarm_reminder_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "alarm-reminders/alarm-morning/events"
        ) == 0
    );
    assert(
        strcmp(
            alarm_reminder_sync_host_request_body(1),
            "{\"alarm_date\":\"2026-10-15\",\"state\":\"dismissed\"}"
        ) == 0
    );
    assert(alarm_reminder_pending_event_count() == 0);
}

void test_stale_event_404_is_discarded() {
    reset_all();
    const alarm_reminder_t local = make_alarm(
        "alarm-removed",
        "Removed",
        7 * 60,
        0x7F,
        5
    );
    assert(alarm_reminder_upsert(&local) == ESP_OK);
    assert(alarm_reminder_mark_missed("alarm-removed", 20261015) == ESP_OK);
    assert(alarm_reminder_pending_event_count() == 1);

    const int statuses[] = {200, 404};
    const char *bodies[] = {
        "{\"data\":{\"alarms\":[]}}",
        "",
    };
    alarm_reminder_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_pending_event_count() == 0);
}

void test_event_failure_stops_draining_and_retries_next_cycle() {
    reset_all();
    const alarm_reminder_t first = make_alarm(
        "alarm-1",
        "First",
        7 * 60,
        0x7F,
        5
    );
    const alarm_reminder_t second = make_alarm(
        "alarm-2",
        "Second",
        8 * 60,
        0x7F,
        5
    );
    assert(alarm_reminder_upsert(&first) == ESP_OK);
    assert(alarm_reminder_upsert(&second) == ESP_OK);
    assert(alarm_reminder_mark_ringing("alarm-1", 20261015) == ESP_OK);
    assert(alarm_reminder_mark_missed("alarm-2", 20261015) == ESP_OK);
    assert(alarm_reminder_pending_event_count() == 2);

    const int failing_statuses[] = {200, 500};
    const char *failing_bodies[] = {
        "{\"data\":{\"alarms\":[]}}",
        "",
    };
    alarm_reminder_sync_host_set_http_sequence(
        failing_statuses,
        failing_bodies,
        2
    );
    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_sync_host_request_count() == 2);
    assert(alarm_reminder_pending_event_count() == 2);

    reset_all();
    assert(alarm_reminder_upsert(&first) == ESP_OK);
    assert(alarm_reminder_upsert(&second) == ESP_OK);
    assert(alarm_reminder_mark_ringing("alarm-1", 20261015) == ESP_OK);
    assert(alarm_reminder_mark_missed("alarm-2", 20261015) == ESP_OK);

    const int retry_statuses[] = {200, 200, 200};
    const char *retry_bodies[] = {
        "{\"data\":{\"alarms\":[]}}",
        "{}",
        "{}",
    };
    alarm_reminder_sync_host_set_http_sequence(
        retry_statuses,
        retry_bodies,
        3
    );
    assert(alarm_reminder_sync_now() == ESP_OK);
    assert(alarm_reminder_sync_host_request_count() == 3);
    assert(alarm_reminder_pending_event_count() == 0);
}

void test_initialize_requires_alarm_store_and_is_idempotent() {
    reset_all();
    const module_descriptor_t *descriptor =
        alarm_reminder_sync_module_descriptor();
    alarm_reminder_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!alarm_reminder_sync_host_is_ready());

    alarm_reminder_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(alarm_reminder_sync_host_is_ready());
    assert(alarm_reminder_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(alarm_reminder_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_and_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        alarm_reminder_sync_module_descriptor();
    alarm_reminder_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_IDLE
    );
    alarm_reminder_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(alarm_reminder_sync_host_task_pending());

    alarm_reminder_sync_host_run_pending_task();
    assert(alarm_reminder_sync_host_request_count() == 0);
    assert(alarm_reminder_sync_host_delay_calls() == 1);
    assert(alarm_reminder_sync_host_task_delete_calls() == 1);
    assert(!alarm_reminder_sync_host_task_pending());
}

void test_worker_syncs_then_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        alarm_reminder_sync_module_descriptor();
    set_single_response(200, "{\"data\":{\"alarms\":[]}}");
    alarm_reminder_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    alarm_reminder_sync_host_run_pending_task();
    assert(alarm_reminder_sync_host_request_count() == 1);
    assert(alarm_reminder_sync_host_delay_calls() == 1);
    assert(alarm_reminder_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_alarm_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_replaces_schedule();
    test_empty_snapshot_clears_schedule();
    test_invalid_responses_leave_cache_untouched();
    test_unauthorized_response_clears_session();
    test_transport_failures_are_propagated();
    test_events_are_reported_and_acknowledged();
    test_stale_event_404_is_discarded();
    test_event_failure_stops_draining_and_retries_next_cycle();
    test_initialize_requires_alarm_store_and_is_idempotent();
    test_worker_retries_offline_and_stops_cleanly();
    test_worker_syncs_then_stops_cleanly();
    return 0;
}
