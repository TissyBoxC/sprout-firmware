#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_srp_gen_salt_verifier(
    const char *username,
    int username_length,
    const char *password,
    int password_length,
    char **salt,
    int salt_length,
    char **verifier,
    int *verifier_length
);

#ifdef __cplusplus
}
#endif
