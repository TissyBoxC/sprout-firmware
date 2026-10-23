#include <cassert>
#include <cstring>

#include "emotion_report.h"
#include "emotion_report_core.h"

int main() {
    // Every companionship emotion keeps its shipped name; the platform owns the
    // neutral/angry/scared aliases, so the device must not translate them.
    const struct {
        companionship_emotion_t emotion;
        const char *label;
    } cases[] = {
        {COMPANIONSHIP_EMOTION_NEUTRAL, "neutral"},
        {COMPANIONSHIP_EMOTION_HAPPY, "happy"},
        {COMPANIONSHIP_EMOTION_SAD, "sad"},
        {COMPANIONSHIP_EMOTION_ANGRY, "angry"},
        {COMPANIONSHIP_EMOTION_SCARED, "scared"},
        {COMPANIONSHIP_EMOTION_TIRED, "tired"},
    };
    for (const auto &entry : cases) {
        emotion_report_observation_t observation = {};
        assert(
            emotion_report_core_make_observation(
                entry.emotion,
                1700000000,
                "6f1f0c8e-0b0c-4c3a-9a2e-1c0d2b3a4e5f",
                &observation
            ) == ESP_OK
        );
        assert(std::strcmp(observation.label, entry.label) == 0);
        assert(observation.confidence == EMOTION_REPORT_CONFIDENCE);
        assert(observation.confidence == 60);
        assert(observation.observed_at_epoch_seconds == 1700000000);
        assert(
            std::strcmp(
                observation.child_id,
                "6f1f0c8e-0b0c-4c3a-9a2e-1c0d2b3a4e5f"
            ) == 0
        );
    }

    // An unknown bucket is refused instead of reported as the string
    // "unknown", which the platform would reject as an invalid label.
    emotion_report_observation_t observation = {};
    assert(
        emotion_report_core_make_observation(
            static_cast<companionship_emotion_t>(99),
            1700000000,
            nullptr,
            &observation
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_HAPPY,
            1700000000,
            nullptr,
            nullptr
        ) == ESP_ERR_INVALID_ARG
    );

    // An untrusted clock must not produce a 1970 timestamp the platform would
    // refuse as too old.
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_HAPPY,
            0,
            nullptr,
            &observation
        ) == ESP_ERR_INVALID_STATE
    );

    // No child id means "the child this device serves"; the field stays empty
    // rather than carrying a device id that the platform would reject.
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_TIRED,
            1700000000,
            nullptr,
            &observation
        ) == ESP_OK
    );
    assert(observation.child_id[0] == '\0');
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_TIRED,
            1700000000,
            "",
            &observation
        ) == ESP_OK
    );
    assert(observation.child_id[0] == '\0');

    // An id that would be truncated is dropped, not truncated into a
    // different child.
    char oversized_id[EMOTION_REPORT_CHILD_ID_SIZE + 8] = {};
    std::memset(oversized_id, 'a', sizeof(oversized_id) - 1);
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_HAPPY,
            1700000000,
            oversized_id,
            &observation
        ) == ESP_OK
    );
    assert(observation.child_id[0] == '\0');

    // RFC 3339 formatting covers a leap day and a second-aligned instant in
    // UTC; the platform parses it with time.RFC3339.
    char observed_at[EMOTION_REPORT_OBSERVED_AT_SIZE] = {};
    assert(
        emotion_report_core_observed_at(1700000000, observed_at, sizeof(observed_at))
    );
    assert(std::strcmp(observed_at, "2023-11-14T22:13:20Z") == 0);
    assert(
        emotion_report_core_observed_at(1709164800, observed_at, sizeof(observed_at))
    );
    assert(std::strcmp(observed_at, "2024-02-29T00:00:00Z") == 0);
    assert(
        emotion_report_core_observed_at(951782400, observed_at, sizeof(observed_at))
    );
    assert(std::strcmp(observed_at, "2000-02-29T00:00:00Z") == 0);
    // A zero instant is "no trusted clock", and a value far outside the
    // documented window is refused rather than formatted into a wrong date.
    assert(!emotion_report_core_observed_at(0, observed_at, sizeof(observed_at)));
    assert(
        !emotion_report_core_observed_at(4102444800, observed_at, sizeof(observed_at))
    );
    char small[8] = {};
    assert(!emotion_report_core_observed_at(1700000000, small, sizeof(small)));

    // The body carries exactly the four accepted fields and nothing else.
    char body[EMOTION_REPORT_BODY_SIZE] = {};
    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_ANGRY,
            1700000000,
            "child-1",
            &observation
        ) == ESP_OK
    );
    const int body_length = emotion_report_core_build_body(
        &observation,
        body,
        sizeof(body)
    );
    assert(body_length > 0);
    assert(
        std::strcmp(
            body,
            "{\"child_id\":\"child-1\",\"label\":\"angry\",\"confidence\":60,"
            "\"observed_at\":\"2023-11-14T22:13:20Z\"}"
        ) == 0
    );
    // Nothing that could carry speech, media, or a reason is in the payload.
    assert(std::strstr(body, "text") == nullptr);
    assert(std::strstr(body, "audio") == nullptr);
    assert(std::strstr(body, "image") == nullptr);
    assert(std::strstr(body, "transcript") == nullptr);

    assert(
        emotion_report_core_make_observation(
            COMPANIONSHIP_EMOTION_HAPPY,
            1700000000,
            nullptr,
            &observation
        ) == ESP_OK
    );
    assert(
        emotion_report_core_build_body(&observation, body, sizeof(body)) > 0
    );
    assert(
        std::strcmp(
            body,
            "{\"label\":\"happy\",\"confidence\":60,"
            "\"observed_at\":\"2023-11-14T22:13:20Z\"}"
        ) == 0
    );
    char tiny[16] = {};
    assert(emotion_report_core_build_body(&observation, tiny, sizeof(tiny)) == -1);
    assert(emotion_report_core_build_body(nullptr, body, sizeof(body)) == -1);

    // Status mapping: a family that turned sensing off answers 403 and the
    // sample must still be retried once the toggle is back on; a 422 is a
    // sample the device cannot fix, so retrying it forever is wrong.
    assert(emotion_report_core_classify_status(201) == EMOTION_REPORT_UPLOAD_OK);
    assert(emotion_report_core_classify_status(204) == EMOTION_REPORT_UPLOAD_OK);
    assert(emotion_report_core_classify_status(0) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_core_classify_status(401) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_core_classify_status(403) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_core_classify_status(429) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_core_classify_status(500) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_core_classify_status(503) == EMOTION_REPORT_UPLOAD_RETRY);
    assert(
        emotion_report_core_classify_status(400) == EMOTION_REPORT_UPLOAD_REJECTED
    );
    assert(
        emotion_report_core_classify_status(404) == EMOTION_REPORT_UPLOAD_REJECTED
    );
    assert(
        emotion_report_core_classify_status(422) == EMOTION_REPORT_UPLOAD_REJECTED
    );

    return 0;
}
