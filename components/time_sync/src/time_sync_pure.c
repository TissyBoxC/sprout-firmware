#include "time_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define TIME_SYNC_SECONDS_PER_DAY 86400

static bool time_sync_timezone_offset_is_valid(int32_t offset_minutes) {
    return offset_minutes >= -840 && offset_minutes <= 840;
}

esp_err_t time_sync_unix_to_local_civil(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    time_sync_local_time_t *local_time_out
) {
    if (local_time_out == NULL ||
        !time_sync_timezone_offset_is_valid(timezone_offset_minutes)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t local_epoch_seconds =
        utc_epoch_seconds + (int64_t)timezone_offset_minutes * 60;
    int64_t local_day_number = local_epoch_seconds / TIME_SYNC_SECONDS_PER_DAY;
    int64_t second_of_day = local_epoch_seconds % TIME_SYNC_SECONDS_PER_DAY;
    if (second_of_day < 0) {
        second_of_day += TIME_SYNC_SECONDS_PER_DAY;
        --local_day_number;
    }

    // Civil-from-days by Howard Hinnant's public-domain algorithm. Keeping
    // this integer-only avoids a device TZ database and makes DST-style fixed
    // offsets host-testable. The algorithm's epoch is 0000-03-01, so the Unix
    // day number is shifted by the published 719468-day offset first.
    int64_t days = local_day_number + 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t day_of_era = days - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
         day_of_era / 146096) /
        365;
    int64_t civil_year = year_of_era + era * 400;
    const int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 -
                      year_of_era / 100);
    const int64_t month_prime = (5 * day_of_year + 2) / 153;
    const int64_t civil_day =
        day_of_year - (153 * month_prime + 2) / 5 + 1;
    const int64_t civil_month =
        month_prime < 10 ? month_prime + 3 : month_prime - 9;
    civil_year += civil_month <= 2 ? 1 : 0;

    local_time_out->year = (int)civil_year;
    local_time_out->month = (int)civil_month;
    local_time_out->day = (int)civil_day;
    local_time_out->hour = (int)(second_of_day / 3600);
    local_time_out->minute = (int)((second_of_day % 3600) / 60);
    local_time_out->second = (int)(second_of_day % 60);
    // Unix day 0 (1970-01-01) is a Thursday, which is 4 with Sunday = 0.
    int64_t weekday = (local_day_number + 4) % 7;
    if (weekday < 0) {
        weekday += 7;
    }
    local_time_out->weekday = (int)weekday;
    return ESP_OK;
}

esp_err_t time_sync_local_day_key(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    int32_t *day_key_out
) {
    if (day_key_out == NULL ||
        !time_sync_timezone_offset_is_valid(timezone_offset_minutes)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t local_epoch_seconds =
        utc_epoch_seconds + (int64_t)timezone_offset_minutes * 60;
    int64_t local_day_number = local_epoch_seconds / TIME_SYNC_SECONDS_PER_DAY;
    if (local_epoch_seconds % TIME_SYNC_SECONDS_PER_DAY < 0) {
        --local_day_number;
    }
    if (local_day_number < INT32_MIN || local_day_number > INT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    *day_key_out = (int32_t)local_day_number;
    return ESP_OK;
}

esp_err_t time_sync_local_minute_of_day(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    int32_t *minute_of_day_out
) {
    if (minute_of_day_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    time_sync_local_time_t local_time = {};
    const esp_err_t result = time_sync_unix_to_local_civil(
        utc_epoch_seconds,
        timezone_offset_minutes,
        &local_time
    );
    if (result != ESP_OK) {
        return result;
    }
    *minute_of_day_out = local_time.hour * 60 + local_time.minute;
    return ESP_OK;
}
