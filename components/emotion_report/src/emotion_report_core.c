#include "emotion_report_core.h"

#include <stdio.h>
#include <string.h>

#include "companionship.h"

/**
 * @brief Copy a bounded string, rejecting a value that would be truncated.
 *
 * A truncated id or label is a different value on the wire, so the caller must
 * drop the sample instead of sending a plausible-looking wrong one.
 */
static bool emotion_report_copy_string(
    char *destination,
    size_t destination_size,
    const char *source
) {
    if (destination_size == 0 || source == NULL || source[0] == '\0') {
        return false;
    }
    const size_t length = strlen(source);
    if (length >= destination_size) {
        return false;
    }
    memcpy(destination, source, length + 1);
    return true;
}

// 1970..2099 keeps the civil-date arithmetic far from overflow while covering
// every instant a device with a trusted clock can legitimately report.
#define EMOTION_REPORT_YEAR_MIN 1970
#define EMOTION_REPORT_YEAR_MAX 2099
#define EMOTION_REPORT_SECONDS_PER_DAY 86400

// Floor division and remainder, so instants before 1970 (never reported, but
// reachable from a hostile clock value) still produce days and seconds with the
// same sign instead of a negative remainder.
static int64_t emotion_report_floor_div(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    if ((value % divisor) < 0) {
        quotient -= 1;
    }
    return quotient;
}

static int emotion_report_floor_mod(int64_t value, int64_t divisor) {
    const int64_t remainder = value % divisor;
    return (int)(remainder < 0 ? remainder + divisor : remainder);
}

// Days since 1970-01-01 to a proleptic Gregorian date. Howard Hinnant's
// civil_from_days, which is exact for the whole range and avoids one length
// table per leap-cycle position.
static void emotion_report_civil_from_days(
    int64_t days,
    int *year_out,
    int *month_out,
    int *day_out
) {
    const int64_t shifted = days + 719468;
    const int64_t era = emotion_report_floor_div(shifted, 146097);
    const int64_t day_of_era = shifted - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
         day_of_era / 146096) /
        365;
    const int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t month_index = (5 * day_of_year + 2) / 153;
    *day_out = (int)(day_of_year - (153 * month_index + 2) / 5 + 1);
    *month_out = (int)(month_index < 10 ? month_index + 3 : month_index - 9);
    *year_out = (int)(year_of_era + era * 400 + (*month_out <= 2 ? 1 : 0));
}

esp_err_t emotion_report_core_make_observation(
    companionship_emotion_t emotion,
    int64_t observed_at_epoch_seconds,
    const char *child_id,
    emotion_report_observation_t *observation_out
) {
    if (observation_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // The platform translates neutral/angry/scared itself, so the device
    // keeps its shipped vocabulary and never renames a stored contract.
    const char *label = companionship_emotion_name(emotion);
    if (label == NULL || strcmp(label, "unknown") == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (observed_at_epoch_seconds <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    emotion_report_observation_t observation = {};
    if (!emotion_report_copy_string(
            observation.label,
            sizeof(observation.label),
            label
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    observation.confidence = EMOTION_REPORT_CONFIDENCE;
    observation.observed_at_epoch_seconds = observed_at_epoch_seconds;
    // An unbound or over-long child id leaves the field empty: the platform
    // then attributes the sample to the child the device currently serves, and
    // a value the device cannot represent exactly is never invented.
    (void)emotion_report_copy_string(
        observation.child_id,
        sizeof(observation.child_id),
        child_id
    );
    *observation_out = observation;
    return ESP_OK;
}

bool emotion_report_core_observed_at(
    int64_t epoch_seconds,
    char *observed_at_out,
    size_t observed_at_size
) {
    if (observed_at_out == NULL || observed_at_size < 21 ||
        epoch_seconds <= 0) {
        return false;
    }
    const int64_t days = emotion_report_floor_div(
        epoch_seconds,
        EMOTION_REPORT_SECONDS_PER_DAY
    );
    int year = 0;
    int month = 0;
    int day = 0;
    emotion_report_civil_from_days(days, &year, &month, &day);
    if (year < EMOTION_REPORT_YEAR_MIN || year > EMOTION_REPORT_YEAR_MAX) {
        return false;
    }
    const int64_t seconds_of_day =
        epoch_seconds - days * EMOTION_REPORT_SECONDS_PER_DAY;
    const int written = snprintf(
        observed_at_out,
        observed_at_size,
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        year,
        month,
        day,
        (int)(seconds_of_day / 3600),
        emotion_report_floor_mod(seconds_of_day, 3600) / 60,
        emotion_report_floor_mod(seconds_of_day, 60)
    );
    return written > 0 && (size_t)written < observed_at_size;
}

int emotion_report_core_build_body(
    const emotion_report_observation_t *observation,
    char *body_out,
    size_t body_size
) {
    if (observation == NULL || body_out == NULL || body_size == 0) {
        return -1;
    }
    char observed_at[EMOTION_REPORT_OBSERVED_AT_SIZE] = {0};
    if (!emotion_report_core_observed_at(
            observation->observed_at_epoch_seconds,
            observed_at,
            sizeof(observed_at)
        )) {
        return -1;
    }
    // child_id is omitted when unknown rather than sent empty, so the request
    // matches the documented shape for every case.
    const int written = observation->child_id[0] != '\0'
        ? snprintf(
            body_out,
            body_size,
            "{\"child_id\":\"%s\",\"label\":\"%s\",\"confidence\":%d,"
            "\"observed_at\":\"%s\"}",
            observation->child_id,
            observation->label,
            observation->confidence,
            observed_at
        )
        : snprintf(
            body_out,
            body_size,
            "{\"label\":\"%s\",\"confidence\":%d,\"observed_at\":\"%s\"}",
            observation->label,
            observation->confidence,
            observed_at
        );
    if (written <= 0 || (size_t)written >= body_size) {
        return -1;
    }
    return written;
}

emotion_report_upload_t emotion_report_core_classify_status(int status_code) {
    if (status_code >= 200 && status_code < 300) {
        return EMOTION_REPORT_UPLOAD_OK;
    }
    if (status_code == 401 || status_code == 403 || status_code == 0 ||
        status_code == 408 || status_code == 429 || status_code >= 500) {
        return EMOTION_REPORT_UPLOAD_RETRY;
    }
    // 400/404/422: the sample, the device binding, or the label does not match
    // what the platform can store, and resending it unchanged cannot help.
    return EMOTION_REPORT_UPLOAD_REJECTED;
}
