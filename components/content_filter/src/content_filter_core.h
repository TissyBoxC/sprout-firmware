#pragma once

#include "content_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure text evaluation used by the runtime and host tests.
 *
 * The function never allocates and never logs the input text. It scans a
 * bounded set of rule families and returns the highest-priority match.
 */
content_filter_action_t content_filter_core_evaluate(
    content_filter_direction_t direction,
    const char *text,
    content_filter_decision_t *decision_out
);

#ifdef __cplusplus
}
#endif
