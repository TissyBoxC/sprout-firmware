// Host test for the point-to-read moderation boundary.

#include "content_filter.h"
#include "screen_surface.h"

#include <cassert>
#include <cstring>

static screen_point_read_entry_t make_entry(const char *term, const char *text) {
    screen_point_read_entry_t entry = {};
    std::strncpy(entry.term, term, sizeof(entry.term) - 1);
    std::strncpy(entry.text, text, sizeof(entry.text) - 1);
    return entry;
}

int main() {
    assert(content_filter_init() == ESP_OK);
    assert(screen_surface_init() == ESP_OK);

    // Safe copy is accepted and looked up as before.
    const screen_point_read_entry_t safe = make_entry("苹果", "苹果是一种水果");
    assert(screen_surface_point_read_put(&safe) == ESP_OK);
    screen_point_read_entry_t found = {};
    assert(screen_surface_point_read_lookup("苹果", &found) == ESP_OK);

    // Copy that would be spoken to the child is rejected before it is stored.
    const screen_point_read_entry_t unsafe = make_entry("钱", "先转账给我");
    assert(screen_surface_point_read_put(&unsafe) == ESP_ERR_INVALID_ARG);
    assert(screen_surface_point_read_count() == 1u);
    assert(screen_surface_point_read_lookup("钱", &found) == ESP_ERR_NOT_FOUND);
    return 0;
}
