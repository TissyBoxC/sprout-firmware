#include <stdint.h>
#include <stdio.h>

#include "device_message_core.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_title_validation() {
    check(
        device_message_title_is_valid("记得喝水"),
        "guardian title must be accepted"
    );
    check(
        !device_message_title_is_valid(""),
        "empty title must be rejected"
    );
    check(
        !device_message_title_is_valid(NULL),
        "NULL title must be rejected"
    );

    char long_title[DEVICE_MESSAGE_TITLE_MAX_RUNES + 2];
    for (int index = 0; index < DEVICE_MESSAGE_TITLE_MAX_RUNES + 1; ++index) {
        long_title[index] = 'a';
    }
    long_title[DEVICE_MESSAGE_TITLE_MAX_RUNES + 1] = '\0';
    check(
        !device_message_title_is_valid(long_title),
        "over-long title must be rejected"
    );
}

void test_body_validation() {
    check(
        device_message_body_is_valid(""),
        "an empty body is allowed alongside a title"
    );
    check(
        device_message_body_is_valid("爸爸妈妈希望你今天多喝水。"),
        "guardian body must be accepted"
    );
    check(
        !device_message_body_is_valid(NULL),
        "NULL body must be rejected"
    );
}

void test_severity_contract() {
    const char *const allowed[] = {"info", "success", "warning", "critical"};
    for (const char *severity : allowed) {
        check(
            device_message_severity_is_valid(severity),
            "allowed severity must be accepted"
        );
    }
    check(
        !device_message_severity_is_valid("panic"),
        "unknown severity must be rejected"
    );
    check(
        !device_message_severity_is_valid(""),
        "empty severity must be rejected"
    );
}

void test_duration_clamp() {
    check(
        device_message_clamp_duration(0) ==
            DEVICE_MESSAGE_DEFAULT_DURATION_SECONDS,
        "zero duration must fall back to the default"
    );
    check(
        device_message_clamp_duration(30) == 30,
        "in-range duration must be preserved"
    );
    check(
        device_message_clamp_duration(301) ==
            DEVICE_MESSAGE_DEFAULT_DURATION_SECONDS,
        "over-max duration must fall back to the default"
    );
}

void test_utf8_rune_count() {
    check(
        device_message_utf8_runes("abc") == 3,
        "ASCII runes must count one each"
    );
    check(
        device_message_utf8_runes("记得喝水") == 4,
        "multi-byte runes must count once each"
    );
    check(
        device_message_utf8_runes(NULL) == 0,
        "NULL must count zero runes"
    );
}

}  // namespace

int main() {
    test_title_validation();
    test_body_validation();
    test_severity_contract();
    test_duration_clamp();
    test_utf8_rune_count();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("device_message core tests passed");
    return 0;
}
