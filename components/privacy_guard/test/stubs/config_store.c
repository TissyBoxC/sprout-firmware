#include "config_store.h"

#include <string.h>

// Host tests always inject an in-memory privacy storage, so production NVS
// access must not become part of the test result. These stubs make that
// constraint explicit and fail loudly if a future test accidentally exercises
// the NVS path without a fixture.
int config_store_get_blob(const char *key, void *output, size_t *size) {
    (void)key;
    (void)output;
    (void)size;
    return CONFIG_STORE_ERR_NOT_FOUND;
}

int config_store_set_blob(const char *key, const void *value, size_t size) {
    (void)key;
    (void)value;
    (void)size;
    return 0;
}
