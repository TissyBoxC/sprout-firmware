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

}  // namespace

int main() {
    test_topic_identity_is_derived();
    test_valid_command_parses();
    test_identity_mismatch_is_rejected();
    test_wrong_schema_or_event_is_rejected();
    test_missing_or_mismatched_request_id_is_rejected();
    test_duplicate_delivery_is_suppressed();
    test_ledger_is_bounded();
    test_ack_envelope_is_contract_complete();
    return 0;
}
