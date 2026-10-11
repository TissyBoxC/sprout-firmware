// Host test for the guardian message moderation boundary.

#include "content_filter.h"
#include "device_message.h"

#include <cassert>
#include <cstring>

static device_message_t make_message(const char *title, const char *body) {
    device_message_t message = {};
    std::strncpy(message.id, "msg-1", sizeof(message.id) - 1);
    std::strncpy(message.title, title, sizeof(message.title) - 1);
    std::strncpy(message.body, body, sizeof(message.body) - 1);
    std::strncpy(message.severity, "info", sizeof(message.severity) - 1);
    message.duration_seconds = 10;
    return message;
}

int main() {
    assert(content_filter_init() == ESP_OK);
    assert(device_message_init() == ESP_OK);

    // Authored copy with no prohibited category is accepted and shown.
    device_message_error_t error = DEVICE_MESSAGE_OK;
    const device_message_t safe =
        make_message("提醒", "晚安，记得刷牙再睡觉");
    assert(device_message_present(&safe, &error) == ESP_OK);
    assert(error == DEVICE_MESSAGE_OK);
    assert(device_message_get_snapshot().accepted == 1u);

    // Copy that matches a prohibited category is rejected before storage.
    const device_message_t unsafe =
        make_message("提示", "先转账给我再去领奖");
    assert(device_message_present(&unsafe, &error) == ESP_ERR_INVALID_ARG);
    assert(error == DEVICE_MESSAGE_ERR_INVALID_PAYLOAD);
    assert(device_message_get_snapshot().rejected == 1u);

    // The last stored message is still the accepted one.
    device_message_t last = {};
    assert(device_message_last(&last));
    assert(std::strcmp(last.title, "提醒") == 0);
    return 0;
}
