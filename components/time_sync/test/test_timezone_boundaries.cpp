#include "time_sync.h"

#include <cassert>
#include <cstdint>

static void test_shanghai_day_boundary(void) {
    int32_t day_key = 0;
    int32_t minute_of_day = 0;

    // 2026-10-08T15:59:59Z is still 2026-10-08 in Asia/Shanghai.
    assert(time_sync_local_day_key(1791475199, 480, &day_key) == ESP_OK);
    const int32_t october_eighth = day_key;
    assert(time_sync_local_minute_of_day(
               1791475199,
               480,
               &minute_of_day
           ) == ESP_OK);
    assert(minute_of_day == 1439);

    // One second later crosses local midnight and must produce the next key.
    assert(time_sync_local_day_key(1791475200, 480, &day_key) == ESP_OK);
    assert(day_key == october_eighth + 1);
    assert(time_sync_local_minute_of_day(
               1791475200,
               480,
               &minute_of_day
           ) == ESP_OK);
    assert(minute_of_day == 0);
}

static void test_negative_offset_day_boundary(void) {
    int32_t day_key = 0;

    // Los Angeles (-480) is still on the previous local date near UTC midnight.
    assert(time_sync_local_day_key(1791475200, -480, &day_key) == ESP_OK);
    int32_t shanghai_key = 0;
    assert(time_sync_local_day_key(1791475200, 480, &shanghai_key) == ESP_OK);
    assert(day_key == shanghai_key - 1);
}

static void test_civil_fields(void) {
    time_sync_local_time_t local_time = {};
    assert(time_sync_unix_to_local_civil(
               1791475200,
               480,
               &local_time
           ) == ESP_OK);
    assert(local_time.year == 2026);
    assert(local_time.month == 10);
    assert(local_time.day == 9);
    assert(local_time.hour == 0);
    assert(local_time.minute == 0);
    assert(local_time.second == 0);
    // 2026-10-09 is a Friday; weekday is Sunday = 0.
    assert(local_time.weekday == 5);
}

static void test_utc_epoch_weekday(void) {
    time_sync_local_time_t local_time = {};
    assert(time_sync_unix_to_local_civil(0, 0, &local_time) == ESP_OK);
    assert(local_time.year == 1970);
    assert(local_time.month == 1);
    assert(local_time.day == 1);
    assert(local_time.weekday == 4);
}

static void test_known_local_date(void) {
    time_sync_local_time_t local_time = {};
    // Day key 900 is 1972-06-19; verify the civil conversion and the day key
    // agree for a mid-range historical date.
    const int64_t epoch = 900 * 86400;
    assert(time_sync_unix_to_local_civil(epoch, 0, &local_time) == ESP_OK);
    assert(local_time.year == 1972);
    assert(local_time.month == 6);
    assert(local_time.day == 19);
    int32_t day_key = 0;
    assert(time_sync_local_day_key(epoch, 0, &day_key) == ESP_OK);
    assert(day_key == 900);
}

static void test_invalid_inputs(void) {
    int32_t value = 0;
    time_sync_local_time_t local_time = {};
    assert(time_sync_local_day_key(0, 900, &value) ==
           ESP_ERR_INVALID_ARG);
    assert(time_sync_local_minute_of_day(0, -900, &value) ==
           ESP_ERR_INVALID_ARG);
    assert(time_sync_unix_to_local_civil(0, 0, nullptr) ==
           ESP_ERR_INVALID_ARG);
    assert(time_sync_local_day_key(0, 0, nullptr) == ESP_ERR_INVALID_ARG);
    assert(time_sync_local_minute_of_day(0, 0, nullptr) ==
           ESP_ERR_INVALID_ARG);
    (void)local_time;
}

int main() {
    test_shanghai_day_boundary();
    test_negative_offset_day_boundary();
    test_civil_fields();
    test_utc_epoch_weekday();
    test_known_local_date();
    test_invalid_inputs();
    return 0;
}
