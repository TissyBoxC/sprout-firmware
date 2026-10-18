#include "ota_rollback_policy.h"

bool ota_rollback_health_probe_failure_is_definitive(
    bool request_completed,
    int http_status_code,
    bool response_invalid
) {
    if (!request_completed || http_status_code < 100 ||
        http_status_code > 599) {
        return false;
    }
    if (response_invalid &&
        http_status_code >= 200 && http_status_code < 300) {
        return true;
    }
    switch (http_status_code) {
        case 400:
        case 401:
        case 403:
        case 404:
        case 405:
        case 410:
        case 422:
            return true;
        default:
            return false;
    }
}
