#include "display_report_core.h"

#include <cassert>

int main() {
    // A default policy reports its brightness with the declared panel size and
    // touch support.
    display_policy_t policy = display_policy_default();
    display_status_t status = display_report_build(&policy, 480, 800, true);
    assert(status.brightness_percent == policy.brightness_percent);
    assert(status.screen_width == 480);
    assert(status.screen_height == 800);
    assert(status.touch_supported == true);

    // A device that does not declare a panel reports zero rather than guessing.
    display_status_t unknown = display_report_build(&policy, 0, 0, false);
    assert(unknown.screen_width == 0);
    assert(unknown.screen_height == 0);
    assert(unknown.touch_supported == false);

    // A brightness above the contract clamps to the platform bound.
    display_policy_t bright = display_policy_default();
    bright.brightness_percent = 200;
    display_status_t clamped = display_report_build(&bright, 480, 800, false);
    assert(clamped.brightness_percent == 100);

    // An out-of-range panel dimension clamps into the platform bound.
    display_status_t wide = display_report_build(&policy, 9000, -10, false);
    assert(wide.screen_width == 8192);
    assert(wide.screen_height == 0);

    // A NULL policy yields the conservative default with the passed panel size.
    display_status_t null_status = display_report_build(nullptr, 320, 240, true);
    assert(null_status.brightness_percent == 0);
    assert(null_status.screen_width == 320);
    assert(null_status.screen_height == 240);
    assert(null_status.touch_supported == true);

    return 0;
}
