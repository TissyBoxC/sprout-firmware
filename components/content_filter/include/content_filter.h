#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum accepted UTF-8 text size including the terminator. */
#define CONTENT_FILTER_MAX_TEXT_BYTES 4096

/** @brief Input or output side of one conversation turn. */
typedef enum {
    CONTENT_FILTER_DIRECTION_INPUT = 0,
    CONTENT_FILTER_DIRECTION_OUTPUT,
    CONTENT_FILTER_DIRECTION_COUNT,
} content_filter_direction_t;

/**
 * @brief Decision applied to one text.
 *
 * CRISIS_INTERVENTION is deliberately distinct from BLOCK: the child's message
 * is accepted and the response path must return a guardian-aware safe reply
 * instead of a generic refusal.
 */
typedef enum {
    CONTENT_FILTER_ACTION_ALLOW = 0,
    CONTENT_FILTER_ACTION_CRISIS_INTERVENTION,
    CONTENT_FILTER_ACTION_BLOCK,
} content_filter_action_t;

/** @brief Stable reason codes shared with the gateway and parent app. */
typedef enum {
    CONTENT_FILTER_REASON_OK = 0,
    CONTENT_FILTER_REASON_NULL_ARGUMENT,
    CONTENT_FILTER_REASON_INVALID_DIRECTION,
    CONTENT_FILTER_REASON_EMPTY_TEXT,
    CONTENT_FILTER_REASON_TEXT_TOO_LONG,
    CONTENT_FILTER_REASON_PROMPT_INJECTION,
    CONTENT_FILTER_REASON_VIOLENCE,
    CONTENT_FILTER_REASON_SEXUAL,
    CONTENT_FILTER_REASON_HORROR,
    CONTENT_FILTER_REASON_ILLEGAL,
    CONTENT_FILTER_REASON_GAMBLING,
    CONTENT_FILTER_REASON_DRUGS,
    CONTENT_FILTER_REASON_PERSONAL_DATA_REQUEST,
    CONTENT_FILTER_REASON_OFFLINE_MEETING,
    CONTENT_FILTER_REASON_EXTERNAL_LINK,
    CONTENT_FILTER_REASON_COMMERCIAL_INDUCEMENT,
    CONTENT_FILTER_REASON_CRISIS_SELF_HARM,
    CONTENT_FILTER_REASON_CRISIS_ABUSE,
    CONTENT_FILTER_REASON_CRISIS_METHOD,
} content_filter_reason_t;

/** @brief Complete decision for one text. */
typedef struct {
    content_filter_action_t action;
    content_filter_reason_t reason;
    content_filter_direction_t direction;
    const char *matched_rule;
    size_t text_length;
} content_filter_decision_t;

/**
 * @brief Evaluate one child input or AI output.
 *
 * Stable reason codes are returned for every rejection. Input that indicates
 * self-harm or abuse returns CRISIS_INTERVENTION, which callers must not treat
 * as a blocked message. Output that teaches self-harm methods is blocked.
 */
content_filter_action_t content_filter_evaluate(
    content_filter_direction_t direction,
    const char *text,
    content_filter_decision_t *decision_out
);

/** @brief Return true when the decision forbids sending the text. */
bool content_filter_decision_is_blocked(
    const content_filter_decision_t *decision
);

/** @brief Return true when the caller must add a safe support response. */
bool content_filter_decision_requires_safe_response(
    const content_filter_decision_t *decision
);

/** @brief Return the stable name for one action. */
const char *content_filter_action_name(content_filter_action_t action);

/** @brief Return the stable name for one reason code. */
const char *content_filter_reason_name(content_filter_reason_t reason);

/** @brief Initialize the filter. */
esp_err_t content_filter_init(void);

/** @brief Return true after successful initialization. */
bool content_filter_is_ready(void);

/** @brief Return the removable-module descriptor for content_filter. */
const module_descriptor_t *content_filter_module_descriptor(void);

#ifdef __cplusplus
}
#endif
