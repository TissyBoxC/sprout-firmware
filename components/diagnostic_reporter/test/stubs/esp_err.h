/*
 * Host-test stub for the ESP-IDF esp_err.h header.
 *
 * The diagnostic state layer is pure validation logic that never calls ESP-IDF
 * APIs, so the host tests only need the type and the success constant. Keep
 * this file limited to what the public header references.
 */
#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
