#include "content_download_state.h"

#include <cassert>
#include <cstring>

static content_download_request_t make_request() {
    content_download_request_t request = {};
    std::strncpy(
        request.package_id,
        "story_demo",
        sizeof(request.package_id) - 1
    );
    std::strncpy(
        request.download_url,
        "https://download.clarkhub.cn/content/story_demo.pkg",
        sizeof(request.download_url) - 1
    );
    std::memset(request.sha256, 'a', 64);
    request.sha256[64] = '\0';
    request.size_bytes = 4096;
    return request;
}

int main() {
    content_download_state_t state = {};
    const content_download_request_t request = make_request();
    assert(content_download_state_init(&state, &request, 2));
    assert(state.status == CONTENT_DOWNLOAD_STATUS_WAITING);

    content_download_transition_t transition = content_download_state_start(&state);
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_DOWNLOADING);
    assert(state.attempt == 1);

    content_download_range_t range = content_download_state_range(&state);
    assert(!range.use_range);

    transition = content_download_state_record_bytes(&state, 1024);
    assert(transition.error == CONTENT_DOWNLOADER_OK);
    range = content_download_state_range(&state);
    assert(range.use_range);
    assert(range.range_start == 1024);
    assert(range.range_end == 4095);

    transition = content_download_state_apply_response(
        &state,
        206,
        1024,
        1024,
        4095,
        3072
    );
    assert(transition.error == CONTENT_DOWNLOADER_OK);
    assert(state.received_bytes == 1024);

    transition = content_download_state_apply_response(
        &state,
        200,
        1024,
        0,
        0,
        4096
    );
    assert(transition.error == CONTENT_DOWNLOADER_OK);
    assert(state.received_bytes == 0);

    transition = content_download_state_record_bytes(&state, 4096);
    assert(transition.error == CONTENT_DOWNLOADER_OK);
    transition = content_download_state_begin_verify(&state);
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_VERIFYING);
    transition = content_download_state_complete(&state);
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_COMPLETE);
    transition = content_download_state_cancel(&state);
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_COMPLETE);
    assert(transition.error == CONTENT_DOWNLOADER_OK);

    assert(content_download_state_init(&state, &request, 1));
    assert(content_download_state_start(&state).status ==
           CONTENT_DOWNLOAD_STATUS_DOWNLOADING);
    transition = content_download_state_fail(
        &state,
        CONTENT_DOWNLOADER_ERR_TRANSPORT
    );
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_FAILED);
    assert(transition.error == CONTENT_DOWNLOADER_ERR_TRANSPORT);

    assert(content_download_state_init(&state, &request, 2));
    assert(content_download_state_start(&state).status ==
           CONTENT_DOWNLOAD_STATUS_DOWNLOADING);
    transition = content_download_state_fail(
        &state,
        CONTENT_DOWNLOADER_ERR_TRANSPORT
    );
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_WAITING);
    assert(content_download_state_start(&state).status ==
           CONTENT_DOWNLOAD_STATUS_DOWNLOADING);
    assert(state.attempt == 2);

    transition = content_download_state_cancel(&state);
    assert(transition.status == CONTENT_DOWNLOAD_STATUS_CANCELED);
    assert(transition.error == CONTENT_DOWNLOADER_ERR_CANCELED);

    return 0;
}
