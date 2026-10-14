#include "device_mqtt.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

const char *const DEVICE_ID = "device_demo_001";
const char *const UUID_COMMAND_ID =
    "11111111-2222-3333-4444-555555555555";

const char *const VALID_COMMAND =
    "{\"schema_version\":\"1.0.0\","
    "\"event_id\":\"event_demo_001\","
    "\"event_type\":\"device.command\","
    "\"occurred_at\":\"2026-10-14T09:00:00Z\","
    "\"producer\":\"device_platform\","
    "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
    "\"request_id\":\"request_demo_001\","
    "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
    "\"command_type\":\"display_message\","
    "\"request_id\":\"request_demo_001\","
    "\"data\":{\"notification_id\":\"notification_demo_001\"}}}";

void test_topic_identity_is_derived() {
    char device_id[DEVICE_MQTT_DEVICE_ID_SIZE] = {0};
    assert(
        device_mqtt_device_id_from_topic(
            "sprout/v1/devices/device_demo_001/commands",
            device_id,
            sizeof(device_id)
        ) == DEVICE_MQTT_OK
    );
    assert(std::strcmp(device_id, "device_demo_001") == 0);

    assert(
        device_mqtt_device_id_from_topic(
            "sprout/v1/devices/device_demo_001/commands/ack",
            device_id,
            sizeof(device_id)
        ) == DEVICE_MQTT_ERR_INVALID_TOPIC
    );
    assert(
        device_mqtt_device_id_from_topic(
            "sprout/v1/devices/a/b/commands",
            device_id,
            sizeof(device_id)
        ) == DEVICE_MQTT_ERR_INVALID_TOPIC
    );
    assert(
        device_mqtt_device_id_from_topic(
            "sprout/v1/devices/Device_Demo_001/commands",
            device_id,
            sizeof(device_id)
        ) == DEVICE_MQTT_ERR_INVALID_TOPIC
    );
}

void test_valid_command_parses() {
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            VALID_COMMAND,
            std::strlen(VALID_COMMAND),
            &command
        ) == DEVICE_MQTT_OK
    );
    assert(std::strcmp(command.device_id, DEVICE_ID) == 0);
    assert(std::strcmp(command.command_id, UUID_COMMAND_ID) == 0);
    assert(std::strcmp(command.command_type, "display_message") == 0);
    assert(std::strcmp(command.request_id, "request_demo_001") == 0);
    // The verbatim data object is retained so the executing module can parse
    // the fields its command type defines.
    assert(
        std::strcmp(
            command.data,
            "{\"notification_id\":\"notification_demo_001\"}"
        ) == 0
    );
    assert(command.data_length == std::strlen(command.data));
}

void test_command_without_data_keeps_empty_object() {
    const char *command_without_data =
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"resync_time\","
        "\"request_id\":\"request_demo_001\"}}";
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            command_without_data,
            std::strlen(command_without_data),
            &command
        ) == DEVICE_MQTT_OK
    );
    assert(command.data[0] == '\0');
    assert(command.data_length == 0);
}

void test_oversized_command_data_is_rejected() {
    // The data bound is tighter than the 2 KiB envelope bound, so keep the
    // whole event under the envelope limit to prove the data check fires.
    static char oversized[DEVICE_MQTT_COMMAND_DATA_SIZE + 512];
    int offset = std::snprintf(
        oversized,
        sizeof(oversized),
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"display_message\","
        "\"request_id\":\"request_demo_001\",\"data\":{\"blob\":\""
    );
    assert(offset > 0 && (size_t)offset < sizeof(oversized));
    // Fill past the data bound measured from the start of the data object, so
    // the retained object itself is over length while the whole event stays
    // under the 2 KiB envelope bound.
    int filled = 0;
    while (filled < DEVICE_MQTT_COMMAND_DATA_SIZE + 64) {
        oversized[offset++] = 'a';
        ++filled;
    }
    oversized[offset++] = '"';
    oversized[offset++] = '}';
    oversized[offset++] = '}';
    oversized[offset++] = '}';
    oversized[offset] = '\0';

    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            oversized,
            std::strlen(oversized),
            &command
        ) == DEVICE_MQTT_ERR_FIELD_TOO_LONG
    );
}

void test_identity_mismatch_is_rejected() {
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            "device_other_002",
            VALID_COMMAND,
            std::strlen(VALID_COMMAND),
            &command
        ) == DEVICE_MQTT_ERR_DEVICE_MISMATCH
    );
}

void test_wrong_schema_or_event_is_rejected() {
    const char *wrong_schema =
        "{\"schema_version\":\"2.0.0\",\"event_type\":\"device.command\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"display_message\","
        "\"request_id\":\"request_demo_001\"}}";
    const char *wrong_event =
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.online\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"display_message\","
        "\"request_id\":\"request_demo_001\"}}";
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            wrong_schema,
            std::strlen(wrong_schema),
            &command
        ) == DEVICE_MQTT_ERR_UNSUPPORTED_SCHEMA
    );
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            wrong_event,
            std::strlen(wrong_event),
            &command
        ) == DEVICE_MQTT_ERR_UNSUPPORTED_EVENT
    );
}

void test_missing_or_mismatched_request_id_is_rejected() {
    const char *mismatched_request =
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"display_message\","
        "\"request_id\":\"request_demo_002\"}}";
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            mismatched_request,
            std::strlen(mismatched_request),
            &command
        ) == DEVICE_MQTT_ERR_FIELD_TOO_LONG
    );
}

void test_duplicate_delivery_is_suppressed() {
    device_mqtt_reset_state();
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            VALID_COMMAND,
            std::strlen(VALID_COMMAND),
            &command
        ) == DEVICE_MQTT_OK
    );
    device_mqtt_command_status_t status = DEVICE_MQTT_COMMAND_STATUS_NEW;
    assert(device_mqtt_record_command(&command, &status) == DEVICE_MQTT_OK);
    assert(status == DEVICE_MQTT_COMMAND_STATUS_NEW);
    assert(device_mqtt_record_command(&command, &status) == DEVICE_MQTT_OK);
    assert(status == DEVICE_MQTT_COMMAND_STATUS_DUPLICATE);
    assert(device_mqtt_is_duplicate(command.command_id));
}

void test_ledger_is_bounded() {
    device_mqtt_reset_state();
    for (int index = 0; index < DEVICE_MQTT_DEDUPE_CAPACITY + 4; ++index) {
        device_mqtt_command_t command = {};
        std::snprintf(
            command.command_id,
            sizeof(command.command_id),
            "command_%08x_0000_0000_0000_000000000000",
            index
        );
        device_mqtt_command_status_t status = DEVICE_MQTT_COMMAND_STATUS_NEW;
        assert(device_mqtt_record_command(&command, &status) == DEVICE_MQTT_OK);
        assert(status == DEVICE_MQTT_COMMAND_STATUS_NEW);
    }
    assert(!device_mqtt_is_duplicate(
        "command_00000000_0000_0000_0000_000000000000"
    ));
    assert(device_mqtt_is_duplicate(
        "command_00000013_0000_0000_0000_000000000000"
    ));
    assert(!device_mqtt_is_duplicate(
        "command_00000003_0000_0000_0000_000000000000"
    ));
}

void test_completed_records_are_replayable() {
    device_mqtt_reset_state();
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            VALID_COMMAND,
            std::strlen(VALID_COMMAND),
            &command
        ) == DEVICE_MQTT_OK
    );
    device_mqtt_command_status_t status = DEVICE_MQTT_COMMAND_STATUS_NEW;
    assert(device_mqtt_record_command(&command, &status) == DEVICE_MQTT_OK);
    assert(status == DEVICE_MQTT_COMMAND_STATUS_NEW);

    device_mqtt_command_record_t record = {};
    assert(
        device_mqtt_lookup_command(command.command_id, &record) ==
        DEVICE_MQTT_OK
    );
    assert(!record.completed);
    assert(std::strcmp(record.status, "") == 0);

    assert(
        device_mqtt_complete_command(
            command.command_id,
            command.request_id,
            "acknowledged",
            "consent_revoked"
        ) == DEVICE_MQTT_OK
    );
    assert(
        device_mqtt_lookup_command(command.command_id, &record) ==
        DEVICE_MQTT_OK
    );
    assert(record.completed);
    assert(std::strcmp(record.status, "acknowledged") == 0);
    assert(std::strcmp(record.result_code, "consent_revoked") == 0);
    assert(std::strcmp(record.request_id, "request_demo_001") == 0);
}

void test_completion_requires_recorded_command() {
    device_mqtt_reset_state();
    assert(
        device_mqtt_complete_command(
            UUID_COMMAND_ID,
            "request_demo_001",
            "acknowledged",
            "ok"
        ) == DEVICE_MQTT_ERR_NOT_FOUND
    );
}

void test_ack_envelope_is_contract_complete() {
    char ack[2048] = {0};
    assert(
        device_mqtt_build_command_ack(
            DEVICE_ID,
            UUID_COMMAND_ID,
            "request_demo_001",
            "2026-10-14T09:00:01Z",
            "acknowledged",
            "ok",
            ack,
            sizeof(ack)
        ) == ESP_OK
    );
    assert(std::strstr(ack, "\"schema_version\":\"1.0.0\"") != nullptr);
    assert(std::strstr(ack, "\"event_type\":\"device.command_ack\"") != nullptr);
    assert(std::strstr(ack, "\"producer\":\"firmware\"") != nullptr);
    assert(std::strstr(ack, "\"id\":\"device_demo_001\"") != nullptr);
    assert(
        std::strstr(
            ack,
            "\"command_id\":\"11111111-2222-3333-4444-555555555555\""
        ) != nullptr
    );
    assert(
        std::strstr(
            ack,
            "\"event_id\":\"command_ack_"
        ) != nullptr
    );
    assert(std::strstr(ack, "\"status\":\"acknowledged\"") != nullptr);
    assert(std::strstr(ack, "\"result_code\":\"ok\"") != nullptr);
    assert(std::strstr(ack, "\"request_id\":\"request_demo_001\"") != nullptr);
}

void test_display_message_adapter_parses_escaped_text() {
    const char *command_json =
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"subject\":{\"type\":\"device\",\"id\":\"device_demo_001\"},"
        "\"request_id\":\"request_demo_001\","
        "\"payload\":{\"command_id\":\"" "11111111-2222-3333-4444-555555555555" "\","
        "\"command_type\":\"display_message\","
        "\"request_id\":\"request_demo_001\","
        "\"data\":{\"notification_id\":\"notification_demo_001\","
        "\"title\":\"\\u665a\\u5b89\",\"body\":\"Line 1\\nLine 2\","
        "\"severity\":\"info\",\"category\":\"message\","
        "\"duration_seconds\":20}}}";
    device_mqtt_command_t command = {};
    assert(
        device_mqtt_parse_command(
            DEVICE_ID,
            command_json,
            std::strlen(command_json),
            &command
        ) == DEVICE_MQTT_OK
    );
    device_mqtt_display_message_t message = {};
    assert(
        device_mqtt_parse_display_message(&command, &message) ==
        DEVICE_MQTT_OK
    );
    assert(
        std::strcmp(message.notification_id, "notification_demo_001") == 0
    );
    assert(std::strcmp(message.title, "\xe6\x99\x9a\xe5\xae\x89") == 0);
    assert(std::strcmp(message.body, "Line 1\nLine 2") == 0);
    assert(std::strcmp(message.severity, "info") == 0);
    assert(std::strcmp(message.category, "message") == 0);
    assert(message.duration_seconds == 20);
}

void test_display_message_requires_identity_and_title() {
    device_mqtt_command_t command = {};
    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "display_message"
    );
    const char *missing_title =
        "{\"notification_id\":\"notification_demo_001\"}";
    std::snprintf(
        command.data,
        sizeof(command.data),
        "%s",
        missing_title
    );
    command.data_length = std::strlen(command.data);
    device_mqtt_display_message_t message = {};
    assert(
        device_mqtt_parse_display_message(&command, &message) ==
        DEVICE_MQTT_ERR_INVALID_PAYLOAD
    );
    assert(message.title[0] == '\0');
}

void test_display_message_rejects_wrong_types() {
    device_mqtt_command_t command = {};
    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "display_message"
    );
    const char *wrong_duration =
        "{\"notification_id\":\"notification_demo_001\","
        "\"title\":\"Title\",\"duration_seconds\":\"20\"}";
    std::snprintf(
        command.data,
        sizeof(command.data),
        "%s",
        wrong_duration
    );
    command.data_length = std::strlen(command.data);
    device_mqtt_display_message_t message = {};
    assert(
        device_mqtt_parse_display_message(&command, &message) ==
        DEVICE_MQTT_ERR_INVALID_PAYLOAD
    );
}

void test_privacy_consent_requires_all_four_booleans() {
    device_mqtt_command_t command = {};
    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "sync_privacy_consent"
    );
    const char *complete =
        "{\"audio_upload\":true,\"image_upload\":false,"
        "\"conversation_history\":true,\"usage_analytics\":false}";
    std::snprintf(command.data, sizeof(command.data), "%s", complete);
    command.data_length = std::strlen(command.data);
    device_mqtt_privacy_consent_t consent = {};
    assert(
        device_mqtt_parse_privacy_consent(&command, &consent) ==
        DEVICE_MQTT_OK
    );
    assert(consent.audio_upload_consent);
    assert(!consent.image_upload_consent);
    assert(consent.conversation_history_consent);
    assert(!consent.usage_analytics_consent);

    const char *partial =
        "{\"audio_upload\":true,\"image_upload\":false,"
        "\"conversation_history\":true}";
    std::snprintf(command.data, sizeof(command.data), "%s", partial);
    command.data_length = std::strlen(command.data);
    assert(
        device_mqtt_parse_privacy_consent(&command, &consent) ==
        DEVICE_MQTT_ERR_INVALID_PAYLOAD
    );
}

void test_adapter_rejects_wrong_command_type() {
    device_mqtt_command_t command = {};
    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "resync_time"
    );
    std::snprintf(command.data, sizeof(command.data), "%s", "{}");
    command.data_length = std::strlen(command.data);
    device_mqtt_display_message_t message = {};
    device_mqtt_privacy_consent_t consent = {};
    assert(
        device_mqtt_parse_display_message(&command, &message) ==
        DEVICE_MQTT_ERR_UNSUPPORTED_COMMAND
    );
    assert(
        device_mqtt_parse_privacy_consent(&command, &consent) ==
        DEVICE_MQTT_ERR_UNSUPPORTED_COMMAND
    );
}

void test_adapter_rejects_trailing_and_partial_data() {
    device_mqtt_command_t command = {};
    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "sync_privacy_consent"
    );
    const char *trailing =
        "{\"audio_upload\":true,\"image_upload\":false,"
        "\"conversation_history\":true,\"usage_analytics\":false} trailing";
    std::snprintf(command.data, sizeof(command.data), "%s", trailing);
    command.data_length = std::strlen(trailing);
    device_mqtt_privacy_consent_t consent = {};
    assert(
        device_mqtt_parse_privacy_consent(&command, &consent) ==
        DEVICE_MQTT_ERR_INVALID_PAYLOAD
    );

    std::snprintf(
        command.command_type,
        sizeof(command.command_type),
        "%s",
        "display_message"
    );
    const char *partial =
        "{\"notification_id\":\"notification_demo_001\"}";
    std::snprintf(command.data, sizeof(command.data), "%s", partial);
    command.data_length = std::strlen(partial);
    device_mqtt_display_message_t message = {};
    assert(
        device_mqtt_parse_display_message(&command, &message) ==
        DEVICE_MQTT_ERR_INVALID_PAYLOAD
    );
}

}  // namespace

int main() {
    test_topic_identity_is_derived();
    test_valid_command_parses();
    test_command_without_data_keeps_empty_object();
    test_oversized_command_data_is_rejected();
    test_identity_mismatch_is_rejected();
    test_wrong_schema_or_event_is_rejected();
    test_missing_or_mismatched_request_id_is_rejected();
    test_duplicate_delivery_is_suppressed();
    test_ledger_is_bounded();
    test_completed_records_are_replayable();
    test_completion_requires_recorded_command();
    test_ack_envelope_is_contract_complete();
    test_display_message_adapter_parses_escaped_text();
    test_display_message_requires_identity_and_title();
    test_display_message_rejects_wrong_types();
    test_privacy_consent_requires_all_four_booleans();
    test_adapter_rejects_wrong_command_type();
    test_adapter_rejects_trailing_and_partial_data();
    return 0;
}
