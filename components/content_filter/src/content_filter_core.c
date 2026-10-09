#include "content_filter_core.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *keyword;
    bool ascii_token;
} content_filter_rule_t;

typedef struct {
    content_filter_reason_t reason;
    const char *matched_rule;
} content_filter_match_t;

static char content_filter_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static bool content_filter_ascii_is_word_character(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '_';
}

static bool content_filter_ascii_token_matches(
    const char *text,
    const char *token
) {
    const size_t token_length = strlen(token);
    if (token_length == 0) {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        size_t index = 0;
        while (index < token_length && cursor[index] != '\0' &&
               content_filter_ascii_lower(cursor[index]) ==
                   content_filter_ascii_lower(token[index])) {
            ++index;
        }
        if (index != token_length) {
            continue;
        }
        const bool left_boundary = cursor == text ||
            !content_filter_ascii_is_word_character(cursor[-1]);
        const bool right_boundary =
            !content_filter_ascii_is_word_character(cursor[token_length]);
        if (left_boundary && right_boundary) {
            return true;
        }
    }
    return false;
}

static bool content_filter_rule_matches(
    const char *text,
    const content_filter_rule_t *rule
) {
    return rule->ascii_token ? content_filter_ascii_token_matches(
                                   text,
                                   rule->keyword
                               )
                             : strstr(text, rule->keyword) != NULL;
}

static bool content_filter_rules_match(
    const char *text,
    const content_filter_rule_t *rules,
    size_t rule_count,
    content_filter_match_t *match_out
) {
    for (size_t index = 0; index < rule_count; ++index) {
        if (!content_filter_rule_matches(text, &rules[index])) {
            continue;
        }
        match_out->reason = CONTENT_FILTER_REASON_OK;
        match_out->matched_rule = rules[index].keyword;
        return true;
    }
    return false;
}

static bool content_filter_match_family(
    const char *text,
    const content_filter_rule_t *rules,
    size_t rule_count,
    content_filter_reason_t reason,
    content_filter_match_t *match_out
) {
    content_filter_match_t local = {};
    if (!content_filter_rules_match(text, rules, rule_count, &local)) {
        return false;
    }
    match_out->reason = reason;
    match_out->matched_rule = local.matched_rule;
    return true;
}

static bool content_filter_text_is_empty(const char *text) {
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0';
         ++cursor) {
        if (*cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
            *cursor != '\n') {
            return false;
        }
    }
    return true;
}

static bool content_filter_find_match(
    const char *text,
    content_filter_direction_t direction,
    content_filter_match_t *match_out
) {
    static const content_filter_rule_t prompt_injection[] = {
        {"ignore previous instructions", true},
        {"ignore all previous", true},
        {"system prompt", true},
        {"reveal your prompt", true},
        {"developer message", true},
        {"jailbreak", true},
        {"忽略之前", false},
        {"忽略以上", false},
        {"系统提示词", false},
        {"泄露提示词", false},
        {"越狱", false},
        {"开发者指令", false},
    };
    static const content_filter_rule_t violence[] = {
        {"kill", true},
        {"murder", true},
        {"stab", true},
        {"shoot", true},
        {"bomb", true},
        {"打死", false},
        {"杀死", false},
        {"杀人", false},
        {"暴力", false},
        {"殴打", false},
        {"掐死", false},
        {"砍人", false},
    };
    static const content_filter_rule_t sexual[] = {
        {"sex", true},
        {"porn", true},
        {"nude", true},
        {"色情", false},
        {"裸体", false},
        {"性行为", false},
        {"做爱", false},
        {"黄片", false},
        {"生殖器", false},
    };
    static const content_filter_rule_t horror[] = {
        {"horror", true},
        {"terrifying", true},
        {"nightmare", true},
        {"恐怖故事", false},
        {"血腥", false},
        {"尸体", false},
        {"鬼片", false},
        {"死亡画面", false},
    };
    static const content_filter_rule_t illegal[] = {
        {"explosive", true},
        {"robbery", true},
        {"steal", true},
        {"hack", true},
        {"炸弹", false},
        {"爆炸物", false},
        {"偷窃", false},
        {"抢劫", false},
        {"黑客", false},
        {"走私", false},
    };
    static const content_filter_rule_t gambling[] = {
        {"gambling", true},
        {"casino", true},
        {"赌博", false},
        {"赌钱", false},
        {"下注", false},
        {"赌场", false},
        {"彩票投注", false},
    };
    static const content_filter_rule_t drugs[] = {
        {"cocaine", true},
        {"heroin", true},
        {"marijuana", true},
        {"meth", true},
        {"毒品", false},
        {"吸毒", false},
        {"海洛因", false},
        {"冰毒", false},
        {"大麻", false},
        {"迷药", false},
    };
    static const content_filter_rule_t personal_data_request[] = {
        {"where do you live", true},
        {"what is your address", true},
        {"your phone number", true},
        {"your real name", true},
        {"你的地址", false},
        {"你住在哪", false},
        {"你的手机号", false},
        {"你的真实姓名", false},
        {"家庭住址", false},
        {"学校名字", false},
    };
    static const content_filter_rule_t offline_meeting[] = {
        {"meet me", true},
        {"meet up", true},
        {"come to my house", true},
        {"见面", false},
        {"来找我", false},
        {"出来见", false},
        {"线下见面", false},
    };
    static const content_filter_rule_t external_link[] = {
        {"click this link", true},
        {"open this url", true},
        {"scan this qr", true},
        {"点击链接", false},
        {"打开网址", false},
        {"扫描二维码", false},
        {"加我微信", false},
        {"加微信", false},
    };
    static const content_filter_rule_t commercial_inducement[] = {
        {"buy now", true},
        {"pay me", true},
        {"转账", false},
        {"付款", false},
        {"购买", false},
        {"充值", false},
        {"红包", false},
        {"打赏", false},
        {"借钱", false},
    };
    static const content_filter_rule_t crisis_self_harm[] = {
        {"suicide", true},
        {"kill myself", true},
        {"self harm", true},
        {"cut myself", true},
        {"want to die", true},
        {"自杀", false},
        {"自残", false},
        {"割腕", false},
        {"不想活", false},
        {"想死", false},
        {"结束生命", false},
        {"轻生", false},
    };
    static const content_filter_rule_t crisis_abuse[] = {
        {"hitting me", true},
        {"beats me", true},
        {"hurt me", true},
        {"abuse me", true},
        {"家暴", false},
        {"被殴打", false},
        {"被虐待", false},
        {"打我", false},
        {"伤害我", false},
        {"被性侵", false},
    };
    static const content_filter_rule_t crisis_method[] = {
        {"how to kill yourself", true},
        {"ways to commit suicide", true},
        {"commit suicide", true},
        {"drink bleach", true},
        {"自杀方法", false},
        {"怎么自杀", false},
        {"割腕方法", false},
        {"怎么伤害自己", false},
    };

    // Output moderation runs the methods check first so an instructional
    // reply cannot slip through as a generic crisis response.
    if (direction == CONTENT_FILTER_DIRECTION_OUTPUT &&
        content_filter_match_family(
            text,
            crisis_method,
            sizeof(crisis_method) / sizeof(crisis_method[0]),
            CONTENT_FILTER_REASON_CRISIS_METHOD,
            match_out
        )) {
        return true;
    }

    if (content_filter_match_family(
            text,
            prompt_injection,
            sizeof(prompt_injection) / sizeof(prompt_injection[0]),
            CONTENT_FILTER_REASON_PROMPT_INJECTION,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            crisis_self_harm,
            sizeof(crisis_self_harm) / sizeof(crisis_self_harm[0]),
            CONTENT_FILTER_REASON_CRISIS_SELF_HARM,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            crisis_abuse,
            sizeof(crisis_abuse) / sizeof(crisis_abuse[0]),
            CONTENT_FILTER_REASON_CRISIS_ABUSE,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            violence,
            sizeof(violence) / sizeof(violence[0]),
            CONTENT_FILTER_REASON_VIOLENCE,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            sexual,
            sizeof(sexual) / sizeof(sexual[0]),
            CONTENT_FILTER_REASON_SEXUAL,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            horror,
            sizeof(horror) / sizeof(horror[0]),
            CONTENT_FILTER_REASON_HORROR,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            illegal,
            sizeof(illegal) / sizeof(illegal[0]),
            CONTENT_FILTER_REASON_ILLEGAL,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            gambling,
            sizeof(gambling) / sizeof(gambling[0]),
            CONTENT_FILTER_REASON_GAMBLING,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            drugs,
            sizeof(drugs) / sizeof(drugs[0]),
            CONTENT_FILTER_REASON_DRUGS,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            personal_data_request,
            sizeof(personal_data_request) /
                sizeof(personal_data_request[0]),
            CONTENT_FILTER_REASON_PERSONAL_DATA_REQUEST,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            offline_meeting,
            sizeof(offline_meeting) / sizeof(offline_meeting[0]),
            CONTENT_FILTER_REASON_OFFLINE_MEETING,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            external_link,
            sizeof(external_link) / sizeof(external_link[0]),
            CONTENT_FILTER_REASON_EXTERNAL_LINK,
            match_out
        )) {
        return true;
    }
    if (content_filter_match_family(
            text,
            commercial_inducement,
            sizeof(commercial_inducement) /
                sizeof(commercial_inducement[0]),
            CONTENT_FILTER_REASON_COMMERCIAL_INDUCEMENT,
            match_out
        )) {
        return true;
    }
    return false;
}

content_filter_action_t content_filter_core_evaluate(
    content_filter_direction_t direction,
    const char *text,
    content_filter_decision_t *decision_out
) {
    if (decision_out == NULL) {
        return CONTENT_FILTER_ACTION_BLOCK;
    }
    decision_out->action = CONTENT_FILTER_ACTION_BLOCK;
    decision_out->reason = CONTENT_FILTER_REASON_OK;
    decision_out->direction = direction;
    decision_out->matched_rule = NULL;
    decision_out->text_length = 0;

    if (text == NULL) {
        decision_out->reason = CONTENT_FILTER_REASON_NULL_ARGUMENT;
        return CONTENT_FILTER_ACTION_BLOCK;
    }
    if (direction != CONTENT_FILTER_DIRECTION_INPUT &&
        direction != CONTENT_FILTER_DIRECTION_OUTPUT) {
        decision_out->reason = CONTENT_FILTER_REASON_INVALID_DIRECTION;
        return CONTENT_FILTER_ACTION_BLOCK;
    }
    const size_t text_length = strlen(text);
    decision_out->text_length = text_length;
    if (text_length == 0 || content_filter_text_is_empty(text)) {
        decision_out->reason = CONTENT_FILTER_REASON_EMPTY_TEXT;
        return CONTENT_FILTER_ACTION_BLOCK;
    }
    if (text_length >= CONTENT_FILTER_MAX_TEXT_BYTES) {
        decision_out->reason = CONTENT_FILTER_REASON_TEXT_TOO_LONG;
        return CONTENT_FILTER_ACTION_BLOCK;
    }

    content_filter_match_t match = {};
    if (!content_filter_find_match(text, direction, &match)) {
        decision_out->action = CONTENT_FILTER_ACTION_ALLOW;
        decision_out->reason = CONTENT_FILTER_REASON_OK;
        return decision_out->action;
    }
    decision_out->reason = match.reason;
    decision_out->matched_rule = match.matched_rule;
    if (direction == CONTENT_FILTER_DIRECTION_INPUT &&
        (match.reason == CONTENT_FILTER_REASON_CRISIS_SELF_HARM ||
         match.reason == CONTENT_FILTER_REASON_CRISIS_ABUSE)) {
        decision_out->action = CONTENT_FILTER_ACTION_CRISIS_INTERVENTION;
        return decision_out->action;
    }
    decision_out->action = CONTENT_FILTER_ACTION_BLOCK;
    return decision_out->action;
}

bool content_filter_decision_is_blocked(
    const content_filter_decision_t *decision
) {
    return decision != NULL &&
        decision->action == CONTENT_FILTER_ACTION_BLOCK;
}

bool content_filter_decision_requires_safe_response(
    const content_filter_decision_t *decision
) {
    return decision != NULL &&
        decision->action == CONTENT_FILTER_ACTION_CRISIS_INTERVENTION;
}
