// ArduinoJson is header-only; this component stays in C++ so policy parsing
// is type-safe while the public C API remains usable by the C composition
// root and the runtime reporter.
#include "ArduinoJson.h"

#include "parent_policy.h"

extern "C" {

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cloud_auth.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "volume_control.h"

#define PARENT_POLICY_KEY_VERSION "pp_version"
#define PARENT_POLICY_KEY_DAILY_LIMIT "pp_daily_limit"
#define PARENT_POLICY_KEY_ALLOWED_CATEGORIES "pp_categories"
#define PARENT_POLICY_KEY_DISABLED_PERIOD_COUNT "pp_period_count"
#define PARENT_POLICY_KEY_DISABLED_PERIOD_PREFIX "pp_period_"
#define PARENT_POLICY_KEY_MAX_VOLUME "pp_max_volume"
#define PARENT_POLICY_KEY_UPDATED_AT "pp_updated_at"

#define PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK 16
#define PARENT_POLICY_DISABLED_PERIOD_CHUNK_COUNT \
    ((PARENT_POLICY_MAX_DISABLED_PERIODS + \
      PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK - 1) / \
     PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK)
#define PARENT_POLICY_DISABLED_PERIOD_KEY_SIZE 17

#define PARENT_POLICY_RESPONSE_SIZE 8192
#define PARENT_POLICY_URL_SIZE 320
#define PARENT_POLICY_SESSION_TOKEN_SIZE DEVICE_BINDING_SESSION_TOKEN_SIZE
#define PARENT_POLICY_MAX_DAILY_LIMIT_MINUTES 720
#define PARENT_POLICY_MAX_VOLUME_PERCENT 100
#define PARENT_POLICY_HTTP_TIMEOUT_MS 15000
#define PARENT_POLICY_SCHEMA_VERSION "1.0.0"

static const char *const TAG = "parent_policy";

typedef struct {
    char body[PARENT_POLICY_RESPONSE_SIZE];
    size_t body_length;
} parent_policy_response_t;

static SemaphoreHandle_t parent_policy_mutex;
static bool parent_policy_ready;
static bool parent_policy_has_cached_policy;
static parent_policy_snapshot_t parent_policy_snapshot;
static parent_policy_status_t parent_policy_status;

static esp_err_t parent_policy_store_snapshot(
    const parent_policy_snapshot_t *snapshot
);

static bool parent_policy_timestamp_is_valid(const char *value) {
    if (value == NULL) {
        return false;
    }
    const char *const fractional_seconds = strchr(value, '.');
    const size_t prefix_length =
        fractional_seconds == NULL
            ? strlen(value)
            : (size_t)(fractional_seconds - value) + 1;
    if (prefix_length > PARENT_POLICY_TIMESTAMP_SIZE) {
        return false;
    }
    char timestamp_prefix[PARENT_POLICY_TIMESTAMP_SIZE] = {0};
    if (fractional_seconds == NULL) {
        memcpy(timestamp_prefix, value, strlen(value) + 1);
    } else {
        memcpy(timestamp_prefix, value, prefix_length);
        timestamp_prefix[prefix_length - 1] = 'Z';
    }
    struct tm parsed = {};
    const char *end = strptime(
        timestamp_prefix,
        "%Y-%m-%dT%H:%M:%SZ",
        &parsed
    );
    if (end == NULL || end[0] != '\0' || timegm(&parsed) <= 0) {
        return false;
    }
    if (fractional_seconds == NULL) {
        return true;
    }
    const char *fraction = fractional_seconds + 1;
    size_t fraction_digits = 0;
    while (*fraction >= '0' && *fraction <= '9') {
        ++fraction_digits;
        ++fraction;
    }
    return fraction_digits > 0 && fraction_digits <= 9 &&
           fraction[0] == 'Z' && fraction[1] == '\0';
}

static bool parent_policy_category_is_valid(const char *category) {
    static const char *const categories[] = {
        "story",
        "nursery_rhyme",
        "poetry",
        "english",
        "encyclopedia",
        "bedtime",
    };
    if (category == NULL || category[0] == '\0') {
        return false;
    }
    for (size_t index = 0;
         index < sizeof(categories) / sizeof(categories[0]);
         ++index) {
        if (strcmp(category, categories[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool parent_policy_time_equals(const char *left, const char *right) {
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool parent_policy_time_is_valid(const char *value) {
    if (value == NULL || strlen(value) != 5 || value[2] != ':') {
        return false;
    }
    const int hour = (value[0] - '0') * 10 + (value[1] - '0');
    const int minute = (value[3] - '0') * 10 + (value[4] - '0');
    return value[0] >= '0' && value[0] <= '9' &&
           value[1] >= '0' && value[1] <= '9' &&
           value[3] >= '0' && value[3] <= '9' &&
           value[4] >= '0' && value[4] <= '9' &&
           hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static void parent_policy_set_status(
    bool has_policy,
    bool is_synchronized,
    parent_policy_refresh_reason_t reason,
    esp_err_t error
) {
    parent_policy_status.has_policy = has_policy;
    parent_policy_status.is_synchronized = is_synchronized;
    parent_policy_status.last_refresh_reason = reason;
    parent_policy_status.last_error = error;
    parent_policy_status.policy_version = parent_policy_snapshot.policy_version;
    parent_policy_status.attempted_at = esp_timer_get_time() / 1000000;
}

static void parent_policy_reset_status(void) {
    parent_policy_status = {};
    parent_policy_status.last_error = ESP_ERR_NOT_FOUND;
}

static esp_err_t parent_policy_response_handler(
    esp_http_client_event_t *event
) {
    parent_policy_response_t *response =
        (parent_policy_response_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining =
        sizeof(response->body) - 1 - response->body_length;
    const size_t copy_length =
        (size_t)event->data_len < remaining
            ? (size_t)event->data_len
            : remaining;
    if (copy_length == 0) {
        return ESP_OK;
    }
    memcpy(
        response->body + response->body_length,
        event->data,
        copy_length
    );
    response->body_length += copy_length;
    response->body[response->body_length] = '\0';
    return ESP_OK;
}

static esp_err_t parent_policy_parse_timestamp(
    JsonVariantConst value,
    char *output,
    size_t output_size
) {
    const char *const text = value.as<const char *>();
    if (!parent_policy_timestamp_is_valid(text)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(text) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, text, strlen(text) + 1);
    return ESP_OK;
}

static esp_err_t parent_policy_parse_categories(
    JsonVariantConst value,
    parent_policy_snapshot_t *snapshot
) {
    JsonArrayConst categories = value.as<JsonArrayConst>();
    if (categories.isNull() || categories.size() == 0 ||
        categories.size() > PARENT_POLICY_MAX_ALLOWED_CATEGORIES) {
        return ESP_ERR_INVALID_ARG;
    }
    for (JsonVariantConst category_value : categories) {
        const char *const category = category_value.as<const char *>();
        if (!parent_policy_category_is_valid(category)) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t index = 0; index < snapshot->allowed_category_count;
             ++index) {
            if (strcmp(snapshot->allowed_categories[index], category) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        const size_t category_index = snapshot->allowed_category_count;
        memcpy(
            snapshot->allowed_categories[category_index],
            category,
            strlen(category) + 1
        );
        ++snapshot->allowed_category_count;
    }
    return ESP_OK;
}

static esp_err_t parent_policy_parse_disabled_periods(
    JsonVariantConst value,
    parent_policy_snapshot_t *snapshot
) {
    JsonArrayConst periods = value.as<JsonArrayConst>();
    if (periods.isNull() ||
        periods.size() > PARENT_POLICY_MAX_DISABLED_PERIODS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (JsonVariantConst period_value : periods) {
        JsonObjectConst period = period_value.as<JsonObjectConst>();
        if (period.isNull()) {
            return ESP_ERR_INVALID_ARG;
        }
        const char *const start_time = period["start_time"].as<const char *>();
        const char *const end_time = period["end_time"].as<const char *>();
        if (!parent_policy_time_is_valid(start_time) ||
            !parent_policy_time_is_valid(end_time) ||
            strcmp(start_time, end_time) == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t index = 0; index < snapshot->disabled_period_count;
             ++index) {
            if (parent_policy_time_equals(
                    snapshot->disabled_periods[index].start_time,
                    start_time
                ) &&
                parent_policy_time_equals(
                    snapshot->disabled_periods[index].end_time,
                    end_time
                )) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        const size_t period_index = snapshot->disabled_period_count;
        memcpy(
            snapshot->disabled_periods[period_index].start_time,
            start_time,
            strlen(start_time) + 1
        );
        memcpy(
            snapshot->disabled_periods[period_index].end_time,
            end_time,
            strlen(end_time) + 1
        );
        ++snapshot->disabled_period_count;
    }
    return ESP_OK;
}

static esp_err_t parent_policy_parse_policy(
    JsonVariantConst data,
    parent_policy_snapshot_t *snapshot
) {
    JsonObjectConst policy = data.as<JsonObjectConst>();
    if (policy.isNull()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const JsonVariantConst version_value = policy["policy_version"];
    const JsonVariantConst daily_limit_value = policy["daily_limit_minutes"];
    const JsonVariantConst max_volume_value = policy["max_volume_percent"];
    const JsonVariantConst source_child_count_value =
        policy["source_child_count"];
    const char *const schema_version =
        policy["schema_version"].as<const char *>();
    const char *const aggregation_mode =
        policy["aggregation_mode"].as<const char *>();
    if (schema_version == NULL ||
        strcmp(schema_version, PARENT_POLICY_SCHEMA_VERSION) != 0 ||
        !version_value.is<int64_t>() || version_value.as<int64_t>() <= 0 ||
        !policy["allowed_categories"].is<JsonArrayConst>() ||
        !policy["disabled_periods"].is<JsonArrayConst>() ||
        !daily_limit_value.is<int>() || daily_limit_value.as<int>() < 0 ||
        daily_limit_value.as<int>() > PARENT_POLICY_MAX_DAILY_LIMIT_MINUTES ||
        !max_volume_value.is<int>() || max_volume_value.as<int>() < 0 ||
        max_volume_value.as<int>() > PARENT_POLICY_MAX_VOLUME_PERCENT ||
        !source_child_count_value.is<int>() ||
        source_child_count_value.as<int>() < 1 ||
        aggregation_mode == NULL ||
        strcmp(aggregation_mode, "most_restrictive") != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    parent_policy_snapshot_t parsed = {};
    parsed.policy_version = version_value.as<int64_t>();
    parsed.daily_limit_minutes = (uint32_t)daily_limit_value.as<int>();
    parsed.max_volume_percent = (uint8_t)max_volume_value.as<int>();
    esp_err_t result = parent_policy_parse_timestamp(
        policy["updated_at"],
        parsed.updated_at,
        sizeof(parsed.updated_at)
    );
    if (result != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result = parent_policy_parse_categories(
        policy["allowed_categories"],
        &parsed
    );
    if (result != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result = parent_policy_parse_disabled_periods(
        policy["disabled_periods"],
        &parsed
    );
    if (result != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *snapshot = parsed;
    return ESP_OK;
}

static esp_err_t parent_policy_serialize_categories(
    const parent_policy_snapshot_t *snapshot,
    char *output,
    size_t output_size
) {
    output[0] = '\0';
    for (size_t index = 0; index < snapshot->allowed_category_count; ++index) {
        const char *const separator = index == 0 ? "" : ",";
        const int written = snprintf(
            output + strlen(output),
            output_size - strlen(output),
            "%s%s",
            separator,
            snapshot->allowed_categories[index]
        );
        if (written <= 0 || (size_t)written >= output_size - strlen(output)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    return ESP_OK;
}

static esp_err_t parent_policy_serialize_disabled_period_chunk(
    const parent_policy_snapshot_t *snapshot,
    size_t start_index,
    char *output,
    size_t output_size
) {
    output[0] = '\0';
    const size_t end_index =
        start_index + PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK <
                snapshot->disabled_period_count
            ? start_index + PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK
            : snapshot->disabled_period_count;
    for (size_t index = start_index; index < end_index; ++index) {
        const char *const separator =
            index == start_index ? "" : ",";
        const parent_policy_disabled_period_t *const period =
            &snapshot->disabled_periods[index];
        const int written = snprintf(
            output + strlen(output),
            output_size - strlen(output),
            "%s%s-%s",
            separator,
            period->start_time,
            period->end_time
        );
        if (written <= 0 || (size_t)written >= output_size - strlen(output)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    return ESP_OK;
}

static esp_err_t parent_policy_store_int64(
    const char *key,
    int64_t value
) {
    char encoded[24] = {0};
    const int written = snprintf(
        encoded,
        sizeof(encoded),
        "%lld",
        (long long)value
    );
    if (written <= 0 || (size_t)written >= sizeof(encoded)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return config_store_set_string(key, encoded);
}

static esp_err_t parent_policy_store_snapshot(
    const parent_policy_snapshot_t *snapshot
) {
    char categories[CONFIG_STORE_VALUE_SIZE] = {0};
    // Remove the commit marker first so a power loss during the field writes
    // can never expose a mixed old/new policy after reboot.
    esp_err_t result = config_store_erase_key(PARENT_POLICY_KEY_VERSION);
    if (result != ESP_OK) {
        return result;
    }
    result = parent_policy_serialize_categories(
        snapshot,
        categories,
        sizeof(categories)
    );
    if (result == ESP_OK) {
        result = parent_policy_store_int64(
            PARENT_POLICY_KEY_DAILY_LIMIT,
            snapshot->daily_limit_minutes
        );
    }
    if (result == ESP_OK) {
        result = config_store_set_string(
            PARENT_POLICY_KEY_ALLOWED_CATEGORIES,
            categories
        );
    }
    if (result == ESP_OK) {
        result = parent_policy_store_int64(
            PARENT_POLICY_KEY_DISABLED_PERIOD_COUNT,
            snapshot->disabled_period_count
        );
    }
    for (size_t chunk = 0;
         result == ESP_OK &&
         chunk < PARENT_POLICY_DISABLED_PERIOD_CHUNK_COUNT;
         ++chunk) {
        char key[PARENT_POLICY_DISABLED_PERIOD_KEY_SIZE] = {0};
        const int key_written = snprintf(
            key,
            sizeof(key),
            PARENT_POLICY_KEY_DISABLED_PERIOD_PREFIX "%02u",
            (unsigned int)chunk
        );
        if (key_written <= 0 ||
            (size_t)key_written >= sizeof(key)) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        char disabled_periods[CONFIG_STORE_VALUE_SIZE] = {0};
        result = parent_policy_serialize_disabled_period_chunk(
            snapshot,
            chunk * PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK,
            disabled_periods,
            sizeof(disabled_periods)
        );
        if (result == ESP_OK) {
            result = config_store_set_string(key, disabled_periods);
        }
    }
    if (result == ESP_OK) {
        result = parent_policy_store_int64(
            PARENT_POLICY_KEY_MAX_VOLUME,
            snapshot->max_volume_percent
        );
    }
    if (result == ESP_OK) {
        result = config_store_set_string(
            PARENT_POLICY_KEY_UPDATED_AT,
            snapshot->updated_at
        );
    }
    if (result == ESP_OK) {
        // The version is the commit marker. A partial flash write must not be
        // loaded as a complete policy with a stale version.
        result = parent_policy_store_int64(
            PARENT_POLICY_KEY_VERSION,
            snapshot->policy_version
        );
    }
    return result;
}

static bool parent_policy_parse_int64(
    const char *text,
    int64_t minimum,
    int64_t maximum,
    int64_t *value_out
) {
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const long long parsed = strtoll(text, &end, 10);
    if (end == text || end[0] != '\0' || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *value_out = (int64_t)parsed;
    return true;
}

static esp_err_t parent_policy_parse_categories_text(
    const char *text,
    parent_policy_snapshot_t *snapshot
) {
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_RESPONSE;
    }
    char working[CONFIG_STORE_VALUE_SIZE] = {0};
    memcpy(working, text, strlen(text) + 1);
    char *save_pointer = NULL;
    for (char *category = strtok_r(working, ",", &save_pointer);
         category != NULL;
         category = strtok_r(NULL, ",", &save_pointer)) {
        if (snapshot->allowed_category_count >=
                PARENT_POLICY_MAX_ALLOWED_CATEGORIES ||
            !parent_policy_category_is_valid(category)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        for (size_t index = 0; index < snapshot->allowed_category_count;
             ++index) {
            if (strcmp(snapshot->allowed_categories[index], category) == 0) {
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        memcpy(
            snapshot->allowed_categories[snapshot->allowed_category_count],
            category,
            strlen(category) + 1
        );
        ++snapshot->allowed_category_count;
    }
    return snapshot->allowed_category_count > 0 ? ESP_OK
                                                : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parent_policy_parse_disabled_periods_text(
    const char *text,
    parent_policy_snapshot_t *snapshot
) {
    if (text == NULL || text[0] == '\0') {
        return ESP_OK;
    }
    char working[CONFIG_STORE_VALUE_SIZE] = {0};
    memcpy(working, text, strlen(text) + 1);
    char *save_pointer = NULL;
    for (char *period = strtok_r(working, ",", &save_pointer);
         period != NULL;
         period = strtok_r(NULL, ",", &save_pointer)) {
        char *const separator = strchr(period, '-');
        if (separator == NULL ||
            snapshot->disabled_period_count >=
                PARENT_POLICY_MAX_DISABLED_PERIODS) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *separator = '\0';
        const char *const start_time = period;
        const char *const end_time = separator + 1;
        if (!parent_policy_time_is_valid(start_time) ||
            !parent_policy_time_is_valid(end_time) ||
            strcmp(start_time, end_time) == 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        for (size_t index = 0; index < snapshot->disabled_period_count;
             ++index) {
            if (parent_policy_time_equals(
                    snapshot->disabled_periods[index].start_time,
                    start_time
                ) &&
                parent_policy_time_equals(
                    snapshot->disabled_periods[index].end_time,
                    end_time
                )) {
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        memcpy(
            snapshot->disabled_periods[snapshot->disabled_period_count].start_time,
            start_time,
            strlen(start_time) + 1
        );
        memcpy(
            snapshot->disabled_periods[snapshot->disabled_period_count].end_time,
            end_time,
            strlen(end_time) + 1
        );
        ++snapshot->disabled_period_count;
    }
    return ESP_OK;
}

static esp_err_t parent_policy_load_cached(void) {
    char version_text[24] = {0};
    char daily_limit_text[16] = {0};
    char categories_text[CONFIG_STORE_VALUE_SIZE] = {0};
    char disabled_period_count_text[8] = {0};
    char max_volume_text[8] = {0};
    char updated_at[PARENT_POLICY_TIMESTAMP_SIZE] = {0};
    esp_err_t result = config_store_get_string(
        PARENT_POLICY_KEY_VERSION,
        version_text,
        sizeof(version_text)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        PARENT_POLICY_KEY_DAILY_LIMIT,
        daily_limit_text,
        sizeof(daily_limit_text)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        PARENT_POLICY_KEY_ALLOWED_CATEGORIES,
        categories_text,
        sizeof(categories_text)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        PARENT_POLICY_KEY_DISABLED_PERIOD_COUNT,
        disabled_period_count_text,
        sizeof(disabled_period_count_text)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        PARENT_POLICY_KEY_MAX_VOLUME,
        max_volume_text,
        sizeof(max_volume_text)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        PARENT_POLICY_KEY_UPDATED_AT,
        updated_at,
        sizeof(updated_at)
    );
    if (result != ESP_OK) {
        return result;
    }

    parent_policy_snapshot_t loaded = {};
    int64_t version = 0;
    int64_t daily_limit = 0;
    int64_t max_volume = 0;
    int64_t disabled_period_count = 0;
    if (!parent_policy_parse_int64(
            version_text,
            1,
            INT64_MAX,
            &version
        ) ||
        !parent_policy_parse_int64(
            daily_limit_text,
            0,
            PARENT_POLICY_MAX_DAILY_LIMIT_MINUTES,
            &daily_limit
        ) ||
        !parent_policy_parse_int64(
            disabled_period_count_text,
            0,
            PARENT_POLICY_MAX_DISABLED_PERIODS,
            &disabled_period_count
        ) ||
        !parent_policy_parse_int64(
            max_volume_text,
            0,
            PARENT_POLICY_MAX_VOLUME_PERCENT,
            &max_volume
        ) ||
        !parent_policy_timestamp_is_valid(updated_at)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result = parent_policy_parse_categories_text(categories_text, &loaded);
    if (result != ESP_OK) {
        return result;
    }
    for (size_t chunk = 0;
         chunk < PARENT_POLICY_DISABLED_PERIOD_CHUNK_COUNT;
         ++chunk) {
        const size_t chunk_start =
            chunk * PARENT_POLICY_DISABLED_PERIODS_PER_CHUNK;
        if (chunk_start >= (size_t)disabled_period_count) {
            break;
        }
        char key[PARENT_POLICY_DISABLED_PERIOD_KEY_SIZE] = {0};
        const int key_written = snprintf(
            key,
            sizeof(key),
            PARENT_POLICY_KEY_DISABLED_PERIOD_PREFIX "%02u",
            (unsigned int)chunk
        );
        if (key_written <= 0 || (size_t)key_written >= sizeof(key)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        char disabled_periods_text[CONFIG_STORE_VALUE_SIZE] = {0};
        result = config_store_get_string(
            key,
            disabled_periods_text,
            sizeof(disabled_periods_text)
        );
        if (result != ESP_OK) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        result = parent_policy_parse_disabled_periods_text(
            disabled_periods_text,
            &loaded
        );
        if (result != ESP_OK) {
            return result;
        }
    }
    if (loaded.disabled_period_count != (size_t)disabled_period_count) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    loaded.policy_version = version;
    loaded.daily_limit_minutes = daily_limit;
    loaded.max_volume_percent = (uint8_t)max_volume;
    memcpy(loaded.updated_at, updated_at, sizeof(loaded.updated_at));
    parent_policy_snapshot = loaded;
    parent_policy_has_cached_policy = true;
    return ESP_OK;
}

static esp_err_t parent_policy_clear_cached_values(void) {
    const char *const keys[] = {
        PARENT_POLICY_KEY_VERSION,
        PARENT_POLICY_KEY_DAILY_LIMIT,
        PARENT_POLICY_KEY_ALLOWED_CATEGORIES,
        PARENT_POLICY_KEY_DISABLED_PERIOD_COUNT,
        PARENT_POLICY_KEY_MAX_VOLUME,
        PARENT_POLICY_KEY_UPDATED_AT,
    };
    esp_err_t first_error = ESP_OK;
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const esp_err_t result = config_store_erase_key(keys[index]);
        if (result != ESP_OK && first_error == ESP_OK) {
            first_error = result;
        }
    }
    for (size_t chunk = 0;
         chunk < PARENT_POLICY_DISABLED_PERIOD_CHUNK_COUNT;
         ++chunk) {
        char key[PARENT_POLICY_DISABLED_PERIOD_KEY_SIZE] = {0};
        const int key_written = snprintf(
            key,
            sizeof(key),
            PARENT_POLICY_KEY_DISABLED_PERIOD_PREFIX "%02u",
            (unsigned int)chunk
        );
        if (key_written <= 0 || (size_t)key_written >= sizeof(key)) {
            if (first_error == ESP_OK) {
                first_error = ESP_ERR_INVALID_SIZE;
            }
            continue;
        }
        const esp_err_t result = config_store_erase_key(key);
        if (result != ESP_OK && first_error == ESP_OK) {
            first_error = result;
        }
    }
    return first_error;
}

static esp_err_t parent_policy_apply_snapshot(
    const parent_policy_snapshot_t *snapshot
) {
    const esp_err_t store_result = parent_policy_store_snapshot(snapshot);
    if (store_result != ESP_OK) {
        return store_result;
    }
    const volume_control_error_t volume_result =
        volume_control_set_max_percent(snapshot->max_volume_percent);
    if (volume_result != VOLUME_CONTROL_OK) {
        // The new ceiling could not be enforced. The persisted policy is the
        // stricter guardian intent, so leave it durable and report the live
        // application failure rather than restoring a more permissive value.
        return ESP_ERR_INVALID_STATE;
    }
    parent_policy_snapshot = *snapshot;
    parent_policy_has_cached_policy = true;
    return ESP_OK;
}

static esp_err_t parent_policy_clear_locked(
    parent_policy_refresh_reason_t reason
) {
    const esp_err_t clear_result = parent_policy_clear_cached_values();
    if (clear_result != ESP_OK) {
        // A partial erase must not weaken the live ceiling while a usable
        // policy may still exist in NVS. Keep both the policy and current
        // volume limit, and report the storage failure to the caller.
        parent_policy_set_status(
            parent_policy_has_cached_policy,
            false,
            reason,
            clear_result
        );
        return clear_result;
    }
    parent_policy_has_cached_policy = false;
    memset(&parent_policy_snapshot, 0, sizeof(parent_policy_snapshot));
    const volume_control_error_t volume_result =
        volume_control_set_max_percent(CONFIG_VOLUME_CONTROL_MAX_PERCENT);
    const esp_err_t result = volume_result == VOLUME_CONTROL_OK
                                 ? ESP_OK
                                 : ESP_ERR_INVALID_STATE;
    parent_policy_set_status(
        false,
        result == ESP_OK,
        reason,
        result
    );
    return result;
}

static esp_err_t parent_policy_parse_error_code(
    const char *body,
    char *output,
    size_t output_size
) {
    if (body == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, body) != DeserializationError::Ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const char *const code = document["error"]["code"].as<const char *>();
    if (code == NULL || code[0] == '\0' || strlen(code) + 1 > output_size) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(output, code, strlen(code) + 1);
    return ESP_OK;
}

static esp_err_t parent_policy_request(
    const char *device_id,
    const char *session_token,
    JsonDocument *document,
    int *status_code_out,
    char *error_code,
    size_t error_code_size
) {
    char base_url[PARENT_POLICY_URL_SIZE] = {0};
    esp_err_t result = device_binding_client_get_platform_base_url(
        base_url,
        sizeof(base_url)
    );
    if (result != ESP_OK) {
        return result;
    }
    if (strncmp(base_url, "https://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[PARENT_POLICY_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/runtime/parent-policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    parent_policy_response_t *const response =
        (parent_policy_response_t *)calloc(1, sizeof(parent_policy_response_t));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t client_config = {};
    client_config.url = url;
    client_config.method = HTTP_METHOD_GET;
    client_config.timeout_ms = PARENT_POLICY_HTTP_TIMEOUT_MS;
    client_config.event_handler = parent_policy_response_handler;
    client_config.user_data = response;
    client_config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        free(response);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    char authorization[PARENT_POLICY_SESSION_TOKEN_SIZE + 16] = {0};
    const int header_written = snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        session_token
    );
    if (header_written <= 0 ||
        (size_t)header_written >= sizeof(authorization)) {
        esp_http_client_cleanup(client);
        free(response);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_set_header(client, "Authorization", authorization);
    memset(authorization, 0, sizeof(authorization));
    result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status_code_out != NULL) {
        *status_code_out = status_code;
    }
    if (result != ESP_OK) {
        free(response);
        return result;
    }
    if (status_code < 200 || status_code >= 300) {
        if (error_code != NULL && error_code_size > 0) {
            (void)parent_policy_parse_error_code(
                response->body,
                error_code,
                error_code_size
            );
        }
        free(response);
        if (status_code == 401 || status_code == 403) {
            return ESP_ERR_INVALID_STATE;
        }
        if (status_code == 404) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (deserializeJson(*document, response->body) !=
        DeserializationError::Ok) {
        free(response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonVariant data = (*document)["data"];
    if (!data.is<JsonObjectConst>()) {
        free(response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    document->set(data);
    free(response);
    return ESP_OK;
}

esp_err_t parent_policy_init(void) {
    if (parent_policy_ready) {
        return ESP_OK;
    }
    if (parent_policy_mutex == NULL) {
        parent_policy_mutex = xSemaphoreCreateMutex();
        if (parent_policy_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!config_store_is_ready() || !volume_control_is_ready()) {
        vSemaphoreDelete(parent_policy_mutex);
        parent_policy_mutex = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    parent_policy_status.is_synchronized = false;
    parent_policy_status.last_error = ESP_ERR_NOT_FOUND;
    parent_policy_status.last_refresh_reason =
        PARENT_POLICY_REFRESH_REASON_NOT_READY;
    const esp_err_t load_result = parent_policy_load_cached();
    if (load_result == CONFIG_STORE_ERR_NOT_FOUND) {
        const volume_control_error_t volume_result =
            volume_control_set_max_percent(
            CONFIG_VOLUME_CONTROL_MAX_PERCENT
        );
        if (volume_result != VOLUME_CONTROL_OK) {
            parent_policy_set_status(
                false,
                false,
                PARENT_POLICY_REFRESH_REASON_VOLUME,
                ESP_ERR_INVALID_STATE
            );
            parent_policy_ready = true;
            return ESP_OK;
        }
        parent_policy_set_status(
            false,
            false,
            PARENT_POLICY_REFRESH_REASON_NONE,
            ESP_ERR_NOT_FOUND
        );
    } else if (load_result == ESP_OK) {
        const volume_control_error_t volume_result =
            volume_control_set_max_percent(
                parent_policy_snapshot.max_volume_percent
            );
        if (volume_result != VOLUME_CONTROL_OK) {
            vSemaphoreDelete(parent_policy_mutex);
            parent_policy_mutex = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        parent_policy_set_status(
            true,
            true,
            PARENT_POLICY_REFRESH_REASON_NONE,
            ESP_OK
        );
    } else {
        // An incomplete cache is unusable. Remove the commit marker as well as
        // the remaining fields so the next boot starts from a known state.
        (void)config_store_erase_key(PARENT_POLICY_KEY_VERSION);
        (void)parent_policy_clear_cached_values();
        parent_policy_has_cached_policy = false;
        memset(&parent_policy_snapshot, 0, sizeof(parent_policy_snapshot));
        (void)volume_control_set_max_percent(
            CONFIG_VOLUME_CONTROL_MAX_PERCENT
        );
        parent_policy_set_status(
            false,
            false,
            PARENT_POLICY_REFRESH_REASON_RESPONSE,
            load_result
        );
    }
    if (!parent_policy_has_cached_policy) {
        parent_policy_status.attempted_at = 0;
    }
    parent_policy_ready = true;
    return ESP_OK;
}

bool parent_policy_is_ready(void) {
    return parent_policy_ready;
}

esp_err_t parent_policy_refresh(void) {
    if (!parent_policy_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = cloud_auth_ensure_authenticated();
    if (result != ESP_OK) {
        if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) == pdTRUE) {
            parent_policy_set_status(
                parent_policy_has_cached_policy,
                false,
                PARENT_POLICY_REFRESH_REASON_NOT_READY,
                result
            );
            xSemaphoreGive(parent_policy_mutex);
        }
        return result;
    }

    char session_token[PARENT_POLICY_SESSION_TOKEN_SIZE] = {0};
    result = device_binding_client_copy_session_token(
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) == pdTRUE) {
            parent_policy_set_status(
                parent_policy_has_cached_policy,
                false,
                PARENT_POLICY_REFRESH_REASON_NOT_READY,
                result
            );
            xSemaphoreGive(parent_policy_mutex);
        }
        return result;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) == pdTRUE) {
            parent_policy_set_status(
                parent_policy_has_cached_policy,
                false,
                PARENT_POLICY_REFRESH_REASON_NOT_READY,
                result
            );
            xSemaphoreGive(parent_policy_mutex);
        }
        return result;
    }

    JsonDocument response;
    int status_code = 0;
    char error_code[64] = {0};
    result = parent_policy_request(
        device_id,
        session_token,
        &response,
        &status_code,
        error_code,
        sizeof(error_code)
    );
    memset(session_token, 0, sizeof(session_token));
    if (status_code == 401) {
        cloud_auth_mark_unauthorized();
    } else if (status_code == 403) {
        cloud_auth_mark_revoked();
    }

    if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        parent_policy_snapshot_t parsed = {};
        const esp_err_t parse_result = parent_policy_parse_policy(
            response.as<JsonVariantConst>(),
            &parsed
        );
        if (parse_result != ESP_OK) {
            parent_policy_set_status(
                parent_policy_has_cached_policy,
                false,
                PARENT_POLICY_REFRESH_REASON_RESPONSE,
                parse_result
            );
            result = ESP_ERR_INVALID_RESPONSE;
        } else if (parent_policy_has_cached_policy &&
                   parsed.policy_version < parent_policy_snapshot.policy_version) {
            parent_policy_set_status(
                true,
                true,
                PARENT_POLICY_REFRESH_REASON_VERSION,
                ESP_ERR_INVALID_VERSION
            );
            result = ESP_ERR_INVALID_VERSION;
        } else if (parent_policy_has_cached_policy &&
                   parsed.policy_version ==
                       parent_policy_snapshot.policy_version) {
            // A repeated read does not change the cache. Avoid needless NVS
            // writes while still marking the session synchronized.
            parent_policy_set_status(
                true,
                true,
                PARENT_POLICY_REFRESH_REASON_NONE,
                ESP_OK
            );
        } else {
            const esp_err_t apply_result =
                parent_policy_apply_snapshot(&parsed);
            if (apply_result != ESP_OK) {
                const parent_policy_refresh_reason_t reason =
                    apply_result == ESP_ERR_INVALID_STATE
                        ? PARENT_POLICY_REFRESH_REASON_VOLUME
                        : PARENT_POLICY_REFRESH_REASON_STORAGE;
                parent_policy_set_status(
                    parent_policy_has_cached_policy,
                    false,
                    reason,
                    apply_result
                );
                result = apply_result;
            } else {
                parent_policy_set_status(
                    true,
                    true,
                    PARENT_POLICY_REFRESH_REASON_NONE,
                    ESP_OK
                );
            }
        }
    } else if (result == ESP_ERR_NOT_FOUND &&
               strcmp(error_code, "parent_policy_not_found") == 0) {
        result = parent_policy_clear_locked(
            PARENT_POLICY_REFRESH_REASON_NOT_FOUND
        );
    } else if (result == ESP_ERR_INVALID_STATE) {
        parent_policy_set_status(
            parent_policy_has_cached_policy,
            false,
            PARENT_POLICY_REFRESH_REASON_UNAUTHORIZED,
            result
        );
    } else if (result == ESP_ERR_NOT_FOUND) {
        // A route-level 404 means the device-session endpoint is not deployed
        // yet. Keep a previously cached policy rather than treating a missing
        // API as a guardian deletion.
        parent_policy_set_status(
            parent_policy_has_cached_policy,
            false,
            PARENT_POLICY_REFRESH_REASON_RESPONSE,
            result
        );
    } else if (result == ESP_ERR_INVALID_RESPONSE) {
        // A non-2xx response that is not an authentication or policy-deletion
        // answer is a server contract or service failure. Keep the last valid
        // policy in force and expose the failure through status.
        parent_policy_set_status(
            parent_policy_has_cached_policy,
            false,
            PARENT_POLICY_REFRESH_REASON_RESPONSE,
            result
        );
    } else {
        parent_policy_set_status(
            parent_policy_has_cached_policy,
            false,
            PARENT_POLICY_REFRESH_REASON_NETWORK,
            result
        );
    }
    xSemaphoreGive(parent_policy_mutex);
    return result;
}

esp_err_t parent_policy_refresh_if_due(void) {
    if (!parent_policy_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool has_policy = parent_policy_has_cached_policy;
    const int64_t attempted_at = parent_policy_status.attempted_at;
    const esp_err_t last_error = parent_policy_status.last_error;
    xSemaphoreGive(parent_policy_mutex);
    const int64_t now = esp_timer_get_time() / 1000000;
    if (attempted_at > 0 &&
        now - attempted_at <
            CONFIG_PARENT_POLICY_REFRESH_INTERVAL_SECONDS) {
        return has_policy ? ESP_OK : last_error;
    }
    return parent_policy_refresh();
}

esp_err_t parent_policy_get_snapshot(
    parent_policy_snapshot_t *snapshot_out
) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!parent_policy_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!parent_policy_has_cached_policy) {
        xSemaphoreGive(parent_policy_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *snapshot_out = parent_policy_snapshot;
    xSemaphoreGive(parent_policy_mutex);
    return ESP_OK;
}

parent_policy_status_t parent_policy_get_status(void) {
    parent_policy_status_t status = {};
    if (!parent_policy_ready ||
        xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return status;
    }
    status = parent_policy_status;
    xSemaphoreGive(parent_policy_mutex);
    return status;
}

esp_err_t parent_policy_clear(void) {
    if (!parent_policy_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(parent_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = parent_policy_clear_locked(
        PARENT_POLICY_REFRESH_REASON_NOT_FOUND
    );
    xSemaphoreGive(parent_policy_mutex);
    return result;
}

void parent_policy_shutdown(void) {
    if (!parent_policy_ready && parent_policy_mutex == NULL) {
        return;
    }
    if (parent_policy_mutex != NULL) {
        vSemaphoreDelete(parent_policy_mutex);
        parent_policy_mutex = NULL;
    }
    parent_policy_ready = false;
    parent_policy_has_cached_policy = false;
    parent_policy_snapshot = {};
    parent_policy_reset_status();
}

const char *parent_policy_refresh_reason_name(
    parent_policy_refresh_reason_t reason
) {
    switch (reason) {
        case PARENT_POLICY_REFRESH_REASON_NONE:
            return "none";
        case PARENT_POLICY_REFRESH_REASON_NOT_READY:
            return "not_ready";
        case PARENT_POLICY_REFRESH_REASON_NETWORK:
            return "network";
        case PARENT_POLICY_REFRESH_REASON_UNAUTHORIZED:
            return "unauthorized";
        case PARENT_POLICY_REFRESH_REASON_NOT_FOUND:
            return "not_found";
        case PARENT_POLICY_REFRESH_REASON_RESPONSE:
            return "invalid_response";
        case PARENT_POLICY_REFRESH_REASON_VERSION:
            return "stale_version";
        case PARENT_POLICY_REFRESH_REASON_STORAGE:
            return "storage";
        case PARENT_POLICY_REFRESH_REASON_VOLUME:
            return "volume";
        case PARENT_POLICY_REFRESH_REASON_INTERNAL:
        default:
            return "internal";
    }
}

const module_descriptor_t *parent_policy_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "parent_policy",
        .version = "1.0.0",
        .initialize = parent_policy_init,
        .shutdown = parent_policy_shutdown,
    };
    return &descriptor;
}

}  // extern "C"
