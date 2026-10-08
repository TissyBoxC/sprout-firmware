#include "usage_ledger.h"

#include <string.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "time_sync.h"
#include "usage_ledger_core.h"

#define USAGE_LEDGER_KEY_STATE "usage_ledger_state"

static const char *const TAG = "usage_ledger";

static bool usage_ledger_ready;
static usage_ledger_state_t usage_ledger_state;
static bool usage_ledger_active;
static int64_t usage_ledger_active_started_ms;
static int64_t usage_ledger_last_flush_ms;
static bool usage_ledger_dirty;
static int64_t usage_ledger_applied_policy_version = -1;
static uint32_t usage_ledger_reported_evictions;

static bool usage_ledger_persist(void) {
    return config_store_set_blob(
               USAGE_LEDGER_KEY_STATE,
               &usage_ledger_state,
               sizeof(usage_ledger_state)
           ) == ESP_OK;
}

static void usage_ledger_restore(void) {
    usage_ledger_state_t restored;
    usage_ledger_core_init_state(&restored);
    size_t restored_size = sizeof(restored);
    if (config_store_get_blob(
            USAGE_LEDGER_KEY_STATE,
            &restored,
            &restored_size
        ) != ESP_OK ||
        restored_size != sizeof(restored) ||
        !usage_ledger_core_state_is_compatible(&restored)) {
        // A layout change or corrupted blob starts from an empty ledger so a
        // stale structure can never be interpreted as valid usage.
        usage_ledger_core_init_state(&usage_ledger_state);
        return;
    }
    usage_ledger_state = restored;
    if (usage_ledger_state.has_current_day) {
        usage_ledger_applied_policy_version =
            usage_ledger_state.current_day.last_policy_version;
    }
}

/**
 * @brief Mark the ledger dirty and flush it when the throttle has elapsed.
 *
 * Counter updates are cheap but each NVS commit is not. Flushing on a fixed
 * interval keeps a power loss bounded to a few seconds without one flash erase
 * per active second.
 */
static esp_err_t usage_ledger_commit(bool force) {
    if (!usage_ledger_dirty && !force) {
        return ESP_OK;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (!force && usage_ledger_last_flush_ms > 0 &&
        now_ms - usage_ledger_last_flush_ms <
            (int64_t)USAGE_LEDGER_FLUSH_INTERVAL_SECONDS * 1000) {
        return ESP_OK;
    }
    if (!usage_ledger_persist()) {
        return ESP_FAIL;
    }
    if (usage_ledger_state.evicted_day_count > usage_ledger_reported_evictions) {
        // A full offline queue drops the oldest day. Surface the data loss once
        // per eviction instead of hiding it behind a silent counter.
        ESP_LOGW(
            TAG,
            "usage queue full; %u day(s) evicted",
            (unsigned int)usage_ledger_state.evicted_day_count
        );
        usage_ledger_reported_evictions = usage_ledger_state.evicted_day_count;
    }
    usage_ledger_dirty = false;
    usage_ledger_last_flush_ms = now_ms;
    return ESP_OK;
}

esp_err_t usage_ledger_init(void) {
    if (usage_ledger_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    usage_ledger_core_init_state(&usage_ledger_state);
    usage_ledger_restore();
    usage_ledger_dirty = false;
    usage_ledger_reported_evictions = usage_ledger_state.evicted_day_count;
    usage_ledger_last_flush_ms = esp_timer_get_time() / 1000;
    usage_ledger_ready = true;
    return ESP_OK;
}

bool usage_ledger_is_ready(void) {
    return usage_ledger_ready;
}

static esp_err_t usage_ledger_apply_day_key(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    int32_t *day_key_out
) {
    return time_sync_local_day_key(
        utc_epoch_seconds,
        timezone_offset_minutes,
        day_key_out
    );
}

esp_err_t usage_ledger_add_active_seconds(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t monotonic_delta_seconds
) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t day_key = 0;
    esp_err_t result = usage_ledger_apply_day_key(
        utc_epoch_seconds,
        timezone_offset_minutes,
        &day_key
    );
    if (result != ESP_OK) {
        return result;
    }
    usage_ledger_core_add_active_seconds(
        &usage_ledger_state,
        day_key,
        timezone_offset_minutes,
        monotonic_delta_seconds
    );
    usage_ledger_dirty = true;
    return usage_ledger_commit(false);
}

esp_err_t usage_ledger_set_active(
    bool active,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes
) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_seconds = 0;
    if (usage_ledger_active && usage_ledger_active_started_ms > 0 &&
        now_ms > usage_ledger_active_started_ms) {
        const int64_t elapsed_ms = now_ms - usage_ledger_active_started_ms;
        elapsed_seconds = (uint32_t)(elapsed_ms / 1000);
    }
    if (elapsed_seconds > 0) {
        const esp_err_t add_result = usage_ledger_add_active_seconds(
            utc_epoch_seconds,
            timezone_offset_minutes,
            elapsed_seconds
        );
        if (add_result != ESP_OK) {
            return add_result;
        }
    }
    usage_ledger_active = active;
    usage_ledger_active_started_ms = active ? now_ms : 0;
    return usage_ledger_commit(false);
}

esp_err_t usage_ledger_record_conversation(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t day_key = 0;
    esp_err_t result = usage_ledger_apply_day_key(
        utc_epoch_seconds,
        timezone_offset_minutes,
        &day_key
    );
    if (result != ESP_OK) {
        return result;
    }
    usage_ledger_core_record_conversation(
        &usage_ledger_state,
        day_key,
        timezone_offset_minutes,
        conversation_seconds
    );
    usage_ledger_dirty = true;
    return usage_ledger_commit(true);
}

esp_err_t usage_ledger_record_content_playback(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *category,
    uint32_t content_seconds
) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    usage_ledger_category_t parsed = USAGE_LEDGER_CATEGORY_STORY;
    if (!usage_ledger_category_from_name(category, &parsed)) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t day_key = 0;
    esp_err_t result = usage_ledger_apply_day_key(
        utc_epoch_seconds,
        timezone_offset_minutes,
        &day_key
    );
    if (result != ESP_OK) {
        return result;
    }
    usage_ledger_core_record_content_playback(
        &usage_ledger_state,
        day_key,
        timezone_offset_minutes,
        parsed,
        content_seconds
    );
    usage_ledger_dirty = true;
    return usage_ledger_commit(true);
}

esp_err_t usage_ledger_record_blocked(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *blocked_reason
) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t day_key = 0;
    esp_err_t result = usage_ledger_apply_day_key(
        utc_epoch_seconds,
        timezone_offset_minutes,
        &day_key
    );
    if (result != ESP_OK) {
        return result;
    }
    usage_ledger_core_record_blocked(
        &usage_ledger_state,
        day_key,
        timezone_offset_minutes,
        blocked_reason
    );
    usage_ledger_dirty = true;
    return usage_ledger_commit(false);
}

esp_err_t usage_ledger_set_applied_policy_version(int64_t policy_version) {
    if (!usage_ledger_ready || policy_version < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usage_ledger_state.has_current_day) {
        return ESP_ERR_INVALID_STATE;
    }
    usage_ledger_applied_policy_version = policy_version;
    usage_ledger_state.current_day.last_policy_version = policy_version;
    usage_ledger_dirty = true;
    return usage_ledger_commit(true);
}

int64_t usage_ledger_get_applied_policy_version(void) {
    if (!usage_ledger_ready) {
        return -1;
    }
    return usage_ledger_applied_policy_version;
}

uint32_t usage_ledger_evicted_day_count(void) {
    if (!usage_ledger_ready) {
        return 0;
    }
    return usage_ledger_state.evicted_day_count;
}

esp_err_t usage_ledger_get_current_day(usage_ledger_day_t *day_out) {
    if (!usage_ledger_ready || day_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usage_ledger_state.has_current_day) {
        return ESP_ERR_NOT_FOUND;
    }
    *day_out = usage_ledger_state.current_day;
    return ESP_OK;
}

esp_err_t usage_ledger_get_pending_day(usage_ledger_day_t *day_out) {
    if (!usage_ledger_ready || day_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usage_ledger_core_take_pending_day(&usage_ledger_state, day_out)) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

uint32_t usage_ledger_pending_day_count(void) {
    if (!usage_ledger_ready) {
        return 0;
    }
    return usage_ledger_state.pending_count;
}

uint32_t usage_ledger_remaining_seconds(uint32_t daily_limit_minutes) {
    if (!usage_ledger_ready) {
        return 0;
    }
    const uint32_t limit_seconds =
        daily_limit_minutes > USAGE_LEDGER_SECONDS_PER_DAY / 60
            ? USAGE_LEDGER_SECONDS_PER_DAY
            : daily_limit_minutes * 60;
    if (!usage_ledger_state.has_current_day) {
        return limit_seconds;
    }
    if (usage_ledger_state.current_day.active_seconds >= limit_seconds) {
        return 0;
    }
    return limit_seconds - usage_ledger_state.current_day.active_seconds;
}

bool usage_ledger_daily_limit_reached(uint32_t daily_limit_minutes) {
    if (!usage_ledger_ready || !usage_ledger_state.has_current_day) {
        return false;
    }
    return usage_ledger_remaining_seconds(daily_limit_minutes) == 0;
}

esp_err_t usage_ledger_get_pending_upload(
    usage_ledger_upload_payload_t *payload_out
) {
    if (!usage_ledger_ready || payload_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    usage_ledger_day_t pending = {};
    if (!usage_ledger_core_take_pending_day(&usage_ledger_state, &pending)) {
        return ESP_ERR_NOT_FOUND;
    }
    usage_ledger_core_build_payload(
        &pending,
        payload_out
    );
    return ESP_OK;
}

esp_err_t usage_ledger_mark_upload_confirmed(int32_t day_key) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usage_ledger_core_confirm_pending_day(&usage_ledger_state, day_key)) {
        return ESP_ERR_NOT_FOUND;
    }
    usage_ledger_dirty = true;
    return usage_ledger_commit(true);
}

esp_err_t usage_ledger_close_current_day(void) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usage_ledger_state.has_current_day) {
        return usage_ledger_commit(true);
    }
    usage_ledger_core_roll_day_if_needed(
        &usage_ledger_state,
        usage_ledger_state.current_day.day_key + 1,
        usage_ledger_state.current_day.timezone_offset_minutes
    );
    usage_ledger_state.has_current_day = false;
    memset(
        &usage_ledger_state.current_day,
        0,
        sizeof(usage_ledger_state.current_day)
    );
    usage_ledger_dirty = true;
    return usage_ledger_commit(true);
}

esp_err_t usage_ledger_flush(void) {
    if (!usage_ledger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    // Fold any in-flight active interval into the current day before writing
    // so a shutdown does not discard the seconds since the last active tick.
    if (usage_ledger_active && usage_ledger_active_started_ms > 0) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms > usage_ledger_active_started_ms) {
            const uint32_t elapsed_seconds =
                (uint32_t)((now_ms - usage_ledger_active_started_ms) / 1000);
            if (elapsed_seconds > 0) {
                const int64_t now_epoch = (int64_t)time(NULL);
                int32_t day_key = 0;
                if (time_sync_local_day_key(
                        now_epoch,
                        time_sync_get_timezone_offset_minutes(),
                        &day_key
                    ) == ESP_OK) {
                    usage_ledger_core_add_active_seconds(
                        &usage_ledger_state,
                        day_key,
                        time_sync_get_timezone_offset_minutes(),
                        elapsed_seconds
                    );
                    usage_ledger_dirty = true;
                }
            }
            usage_ledger_active_started_ms = now_ms;
        }
    }
    return usage_ledger_commit(true);
}

const char *usage_ledger_category_name(usage_ledger_category_t category) {
    switch (category) {
        case USAGE_LEDGER_CATEGORY_STORY:
            return "story";
        case USAGE_LEDGER_CATEGORY_NURSERY_RHYME:
            return "nursery_rhyme";
        case USAGE_LEDGER_CATEGORY_POETRY:
            return "poetry";
        case USAGE_LEDGER_CATEGORY_ENGLISH:
            return "english";
        case USAGE_LEDGER_CATEGORY_ENCYCLOPEDIA:
            return "encyclopedia";
        case USAGE_LEDGER_CATEGORY_BEDTIME:
            return "bedtime";
        default:
            return NULL;
    }
}

bool usage_ledger_category_from_name(
    const char *category_name,
    usage_ledger_category_t *category_out
) {
    if (category_name == NULL || category_out == NULL) {
        return false;
    }
    for (usage_ledger_category_t category = USAGE_LEDGER_CATEGORY_STORY;
         category < USAGE_LEDGER_CATEGORY_COUNT;
         category = (usage_ledger_category_t)(category + 1)) {
        const char *name = usage_ledger_category_name(category);
        if (name != NULL && strcmp(name, category_name) == 0) {
            *category_out = category;
            return true;
        }
    }
    return false;
}

void usage_ledger_shutdown(void) {
    if (usage_ledger_ready) {
        // Flush before clearing readiness so a reboot loses at most the in-RAM
        // delta since the last throttled commit, and never more.
        (void)usage_ledger_flush();
    }
    usage_ledger_active = false;
    usage_ledger_active_started_ms = 0;
    usage_ledger_ready = false;
}

const module_descriptor_t *usage_ledger_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "usage_ledger",
        .version = "1.0.0",
        .initialize = usage_ledger_init,
        .shutdown = usage_ledger_shutdown,
    };
    return &descriptor;
}
