#include "ed25519_verify.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void write_hex(uint8_t *output, const char *hex) {
    for (size_t index = 0; index < std::strlen(hex) / 2; ++index) {
        unsigned value = 0;
        std::sscanf(hex + index * 2, "%2x", &value);
        output[index] = static_cast<uint8_t>(value);
    }
}

void test_rfc8032_empty_message_vector() {
    uint8_t public_key[ED25519_VERIFY_PUBLIC_KEY_BYTES] = {};
    uint8_t signature[ED25519_VERIFY_SIGNATURE_BYTES] = {};
    write_hex(
        public_key,
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    );
    write_hex(
        signature,
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    );
    check(
        ed25519_verify(public_key, signature, nullptr, 0),
        "RFC 8032 empty-message vector must verify"
    );
}

void test_tampering_is_rejected() {
    uint8_t public_key[ED25519_VERIFY_PUBLIC_KEY_BYTES] = {};
    uint8_t signature[ED25519_VERIFY_SIGNATURE_BYTES] = {};
    write_hex(
        public_key,
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    );
    write_hex(
        signature,
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    );
    signature[0] ^= 1U;
    check(
        !ed25519_verify(public_key, signature, nullptr, 0),
        "tampered signature must fail"
    );

    signature[0] ^= 1U;
    const uint8_t message = 'x';
    check(
        !ed25519_verify(public_key, signature, &message, 1),
        "different message must fail"
    );
}

void test_null_inputs_fail_closed() {
    const uint8_t public_key[ED25519_VERIFY_PUBLIC_KEY_BYTES] = {};
    const uint8_t signature[ED25519_VERIFY_SIGNATURE_BYTES] = {};
    check(!ed25519_verify(nullptr, signature, nullptr, 0), "null key");
    check(!ed25519_verify(public_key, nullptr, nullptr, 0), "null signature");
    check(
        !ed25519_verify(public_key, signature, nullptr, 1),
        "null message with non-zero size"
    );
}

}  // namespace

int main() {
    test_rfc8032_empty_message_vector();
    test_tampering_is_rejected();
    test_null_inputs_fail_closed();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ed25519 verify tests passed");
    return 0;
}
