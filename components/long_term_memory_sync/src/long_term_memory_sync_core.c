#include "long_term_memory_sync_core.h"

#include <stdio.h>
#include <string.h>

// Confidence is a Q16 weight in the range 0..65536. A server value outside the
// range would silently distort the recall ranking, so it is treated as
// unusable instead of being clamped. The upper bound lives in the header
// because the parsing boundary needs it for the wire-ratio conversion.
#define LONG_TERM_MEMORY_SYNC_CONFIDENCE_MIN 0

void long_term_memory_sync_state_reset(long_term_memory_sync_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->revision = 0;
    state->has_snapshot = false;
}

static bool long_term_memory_sync_text_fits(
    const char *value,
    size_t destination_size
) {
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    return length > 0 && length < destination_size;
}

bool long_term_memory_sync_holds_key(
    const long_term_memory_entry_t *entries,
    size_t entry_count,
    const char *child_id,
    const char *memory_key
) {
    if (entries == NULL || child_id == NULL || memory_key == NULL) {
        return false;
    }
    for (size_t index = 0; index < entry_count; ++index) {
        if (strcmp(entries[index].child_id, child_id) == 0 &&
            strcmp(entries[index].key, memory_key) == 0) {
            return true;
        }
    }
    return false;
}

bool long_term_memory_sync_parse_remote(
    long_term_memory_sync_remote_t *remote_out,
    size_t capacity,
    size_t *remote_count_out,
    size_t entry_count,
    const char *const *keys,
    const char *const *values,
    const int32_t *confidences,
    const int64_t *revisions,
    const int64_t *updated_at_epoch_seconds,
    int64_t response_revision,
    int64_t current_revision,
    bool has_snapshot
) {
    if (remote_count_out == NULL) {
        return false;
    }
    *remote_count_out = 0;
    if (capacity > 0 && remote_out == NULL) {
        return false;
    }
    if (entry_count > 0 &&
        (keys == NULL || values == NULL || confidences == NULL ||
         revisions == NULL || updated_at_epoch_seconds == NULL)) {
        return false;
    }
    if (response_revision < 0) {
        return false;
    }
    // A snapshot revision that moves backwards means the device is talking to
    // a different account or the server lost data; applying it could resurrect
    // facts the guardian deleted.
    if (has_snapshot && response_revision < current_revision) {
        return false;
    }
    if (entry_count > capacity) {
        return false;
    }

    size_t parsed = 0;
    for (size_t index = 0; index < entry_count; ++index) {
        if (!long_term_memory_sync_text_fits(
                keys[index],
                LONG_TERM_MEMORY_KEY_SIZE
            ) ||
            !long_term_memory_sync_text_fits(
                values[index],
                LONG_TERM_MEMORY_VALUE_SIZE
            )) {
            continue;
        }
        if (confidences[index] < LONG_TERM_MEMORY_SYNC_CONFIDENCE_MIN ||
            confidences[index] > LONG_TERM_MEMORY_SYNC_CONFIDENCE_MAX) {
            continue;
        }
        bool is_duplicate = false;
        for (size_t cursor = 0; cursor < parsed; ++cursor) {
            if (strcmp(remote_out[cursor].memory_key, keys[index]) == 0) {
                is_duplicate = true;
                break;
            }
        }
        if (is_duplicate) {
            continue;
        }
        long_term_memory_sync_remote_t *entry = &remote_out[parsed];
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->memory_key, keys[index], strlen(keys[index]) + 1);
        memcpy(entry->memory_value, values[index], strlen(values[index]) + 1);
        entry->confidence = confidences[index];
        entry->revision = revisions[index] > 0 ? revisions[index] : 0;
        entry->updated_at_epoch_seconds = updated_at_epoch_seconds[index];
        parsed += 1;
    }
    // A non-empty response that reduces to nothing usable is malformed. Letting
    // it through would present an empty snapshot and wipe the local store.
    if (parsed == 0 && entry_count > 0) {
        return false;
    }
    *remote_count_out = parsed;
    return true;
}

bool long_term_memory_sync_child_id(
    char *destination,
    size_t destination_size,
    const char *device_id
) {
    if (destination == NULL || destination_size == 0 || device_id == NULL) {
        return false;
    }
    destination[0] = '\0';
    if (device_id[0] == '\0') {
        return false;
    }
    const int written = snprintf(
        destination,
        destination_size,
        "sync:%s",
        device_id
    );
    if (written <= 0 || (size_t)written >= destination_size) {
        // snprintf leaves a truncated prefix behind; clearing it keeps a
        // partial id from being stored as a different child.
        destination[0] = '\0';
        return false;
    }
    return true;
}

bool long_term_memory_sync_pending_contains(
    const char *const *pending_keys,
    size_t pending_count,
    const char *memory_key
) {
    if (pending_keys == NULL || memory_key == NULL) {
        return false;
    }
    for (size_t index = 0; index < pending_count; ++index) {
        if (pending_keys[index] != NULL &&
            strcmp(pending_keys[index], memory_key) == 0) {
            return true;
        }
    }
    return false;
}

bool long_term_memory_sync_pending_add(
    char (*pending_keys)[LONG_TERM_MEMORY_KEY_SIZE],
    size_t capacity,
    size_t *pending_count_in_out,
    const char *memory_key
) {
    if (pending_keys == NULL || pending_count_in_out == NULL) {
        return false;
    }
    if (!long_term_memory_sync_text_fits(
            memory_key,
            LONG_TERM_MEMORY_KEY_SIZE
        )) {
        return false;
    }
    // The store is a contiguous array of fixed-size rows, so each row is
    // searched by string comparison rather than by reinterpreting the array as
    // a pointer table.
    for (size_t index = 0; index < *pending_count_in_out; ++index) {
        if (strcmp(pending_keys[index], memory_key) == 0) {
            return true;
        }
    }
    if (*pending_count_in_out >= capacity) {
        return false;
    }
    const size_t length = strlen(memory_key);
    memcpy(
        pending_keys[*pending_count_in_out],
        memory_key,
        length + 1
    );
    *pending_count_in_out += 1;
    return true;
}
