#include "ota_download_core.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_progress_and_oversize_fail_closed() {
    ota_download_state_t state = {};
    ota_download_state_init(&state, 100, 3);
    check(
        ota_download_state_record_bytes(&state, 70) ==
            OTA_DOWNLOAD_CORE_OK,
        "partial download must be accepted"
    );
    check(state.received_size == 70, "received size must advance");
    check(
        ota_download_state_record_bytes(&state, 31) ==
            OTA_DOWNLOAD_CORE_ERR_OVERSIZED,
        "oversized chunk must fail closed"
    );
    check(state.canceled, "oversized download must be canceled");
}

void test_resume_requires_range_support() {
    ota_download_state_t state = {};
    ota_download_state_init(&state, 100, 3);
    (void)ota_download_state_record_bytes(&state, 20);
    state.range_supported = false;
    check(
        !ota_download_state_should_resume(&state),
        "partial download must not resume without range support"
    );
    state.range_supported = true;
    check(
        ota_download_state_should_resume(&state),
        "partial download must resume with range support"
    );
    check(
        ota_download_state_resume_offset(&state) == 20,
        "resume offset must equal received size"
    );
}

void test_retry_budget() {
    ota_download_state_t state = {};
    ota_download_state_init(&state, 100, 3);
    check(
        ota_download_state_record_failure(&state) ==
            OTA_DOWNLOAD_CORE_OK,
        "first failure must remain retryable"
    );
    check(
        ota_download_state_record_failure(&state) ==
            OTA_DOWNLOAD_CORE_OK,
        "second failure must remain retryable"
    );
    check(
        ota_download_state_record_failure(&state) ==
            OTA_DOWNLOAD_CORE_ERR_RETRY_EXHAUSTED,
        "third failure must exhaust retry budget"
    );
}

void test_completion_hash_and_size() {
    const char *const hash =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    ota_download_state_t state = {};
    ota_download_state_init(&state, 4, 3);
    (void)ota_download_state_record_bytes(&state, 4);
    check(
        ota_download_state_complete(&state, hash, hash) ==
            OTA_DOWNLOAD_CORE_OK,
        "matching size and hash must complete"
    );
    check(state.complete, "completed state must be latched");

    ota_download_state_init(&state, 4, 3);
    (void)ota_download_state_record_bytes(&state, 3);
    check(
        ota_download_state_complete(&state, hash, hash) ==
            OTA_DOWNLOAD_CORE_ERR_SIZE_MISMATCH,
        "short image must fail size validation"
    );

    ota_download_state_init(&state, 4, 3);
    (void)ota_download_state_record_bytes(&state, 4);
    check(
        ota_download_state_complete(
            &state,
            hash,
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        ) == OTA_DOWNLOAD_CORE_ERR_HASH_MISMATCH,
        "hash mismatch must fail closed"
    );
}

void test_cancellation_is_idempotent() {
    ota_download_state_t state = {};
    ota_download_state_init(&state, 100, 3);
    check(
        ota_download_state_cancel(&state) == OTA_DOWNLOAD_CORE_OK,
        "cancel must succeed"
    );
    check(
        ota_download_state_record_bytes(&state, 1) ==
            OTA_DOWNLOAD_CORE_ERR_CANCELED,
        "canceled state must reject more bytes"
    );
    check(
        ota_download_state_record_failure(&state) ==
            OTA_DOWNLOAD_CORE_ERR_CANCELED,
        "canceled state must not consume retries"
    );
}

}  // namespace

int main() {
    test_progress_and_oversize_fail_closed();
    test_resume_requires_range_support();
    test_retry_budget();
    test_completion_hash_and_size();
    test_cancellation_is_idempotent();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota download core tests passed");
    return 0;
}
