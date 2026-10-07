#include "content_package_state.h"

#include <cassert>
#include <cstring>

int main() {
    content_package_runtime_t runtime = {};
    content_package_runtime_init(&runtime);
    assert(runtime.state == CONTENT_PACKAGE_STATE_IDLE);

    content_package_runtime_request_pause(&runtime);
    assert(runtime.paused);
    assert(runtime.state == CONTENT_PACKAGE_STATE_PAUSED);

    content_package_runtime_request_resume(&runtime);
    assert(!runtime.paused);
    assert(runtime.state == CONTENT_PACKAGE_STATE_IDLE);

    content_package_runtime_request_sync(&runtime);
    assert(runtime.sync_requested);
    content_package_runtime_mark_manifest_started(&runtime);
    assert(runtime.state == CONTENT_PACKAGE_STATE_SYNCING_MANIFEST);
    content_package_runtime_mark_manifest_succeeded(&runtime, 4, 2);
    assert(runtime.state == CONTENT_PACKAGE_STATE_DOWNLOADING);

    content_package_runtime_mark_download(
        &runtime,
        "story_demo",
        1024,
        4096
    );
    assert(std::strcmp(runtime.active_package_id, "story_demo") == 0);
    assert(runtime.active_received_bytes == 1024);

    content_package_runtime_mark_failed(
        &runtime,
        CONTENT_PACKAGE_ERR_DOWNLOAD
    );
    assert(runtime.state == CONTENT_PACKAGE_STATE_FAILED);
    assert(runtime.last_error == CONTENT_PACKAGE_ERR_DOWNLOAD);

    content_package_runtime_mark_manifest_succeeded(&runtime, 4, 0);
    content_package_runtime_mark_complete(&runtime);
    assert(runtime.state == CONTENT_PACKAGE_STATE_COMPLETE);
    assert(runtime.active_package_id[0] == '\0');

    const content_package_snapshot_t snapshot =
        content_package_runtime_snapshot(&runtime);
    assert(snapshot.ready_packages == 4);
    assert(snapshot.pending_downloads == 0);
    return 0;
}
