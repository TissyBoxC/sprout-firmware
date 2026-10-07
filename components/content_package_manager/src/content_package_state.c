#include "content_package_state.h"

#include <string.h>

void content_package_runtime_init(content_package_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = CONTENT_PACKAGE_STATE_IDLE;
    runtime->last_error = CONTENT_PACKAGE_OK;
}

void content_package_runtime_request_sync(
    content_package_runtime_t *runtime
) {
    if (runtime == NULL) {
        return;
    }
    runtime->sync_requested = true;
    runtime->command = CONTENT_PACKAGE_COMMAND_SYNC;
    runtime->last_error = CONTENT_PACKAGE_OK;
}

void content_package_runtime_request_pause(
    content_package_runtime_t *runtime
) {
    if (runtime == NULL) {
        return;
    }
    runtime->paused = true;
    runtime->command = CONTENT_PACKAGE_COMMAND_PAUSE;
    runtime->state = CONTENT_PACKAGE_STATE_PAUSED;
}

void content_package_runtime_request_resume(
    content_package_runtime_t *runtime
) {
    if (runtime == NULL) {
        return;
    }
    runtime->paused = false;
    runtime->command = CONTENT_PACKAGE_COMMAND_RESUME;
    runtime->state = CONTENT_PACKAGE_STATE_IDLE;
}

void content_package_runtime_mark_manifest_started(
    content_package_runtime_t *runtime
) {
    if (runtime == NULL) {
        return;
    }
    runtime->running = true;
    runtime->state = CONTENT_PACKAGE_STATE_SYNCING_MANIFEST;
    runtime->last_error = CONTENT_PACKAGE_OK;
}

void content_package_runtime_mark_manifest_succeeded(
    content_package_runtime_t *runtime,
    size_t ready_packages,
    size_t pending_downloads
) {
    if (runtime == NULL) {
        return;
    }
    runtime->ready_packages = ready_packages;
    runtime->pending_downloads = pending_downloads;
    runtime->state = pending_downloads == 0
        ? CONTENT_PACKAGE_STATE_COMPLETE
        : CONTENT_PACKAGE_STATE_DOWNLOADING;
    runtime->last_error = CONTENT_PACKAGE_OK;
}

void content_package_runtime_mark_catalog_revision(
    content_package_runtime_t *runtime,
    int64_t catalog_revision
) {
    if (runtime == NULL) {
        return;
    }
    runtime->catalog_revision = catalog_revision;
}

void content_package_runtime_mark_download(
    content_package_runtime_t *runtime,
    const char *package_id,
    uint64_t received_bytes,
    uint64_t total_bytes
) {
    if (runtime == NULL) {
        return;
    }
    runtime->state = runtime->paused
        ? CONTENT_PACKAGE_STATE_PAUSED
        : CONTENT_PACKAGE_STATE_DOWNLOADING;
    if (package_id != NULL) {
        memcpy(
            runtime->active_package_id,
            package_id,
            sizeof(runtime->active_package_id)
        );
        runtime->active_package_id[
            sizeof(runtime->active_package_id) - 1
        ] = '\0';
    }
    runtime->active_received_bytes = received_bytes;
    runtime->active_total_bytes = total_bytes;
}

void content_package_runtime_mark_failed(
    content_package_runtime_t *runtime,
    content_package_error_t error
) {
    if (runtime == NULL) {
        return;
    }
    runtime->state = CONTENT_PACKAGE_STATE_FAILED;
    runtime->last_error = error;
}

void content_package_runtime_mark_complete(
    content_package_runtime_t *runtime
) {
    if (runtime == NULL) {
        return;
    }
    runtime->running = false;
    runtime->state = CONTENT_PACKAGE_STATE_COMPLETE;
    runtime->active_package_id[0] = '\0';
    runtime->active_received_bytes = 0;
    runtime->active_total_bytes = 0;
}

content_package_snapshot_t content_package_runtime_snapshot(
    const content_package_runtime_t *runtime
) {
    content_package_snapshot_t snapshot = {};
    if (runtime == NULL) {
        snapshot.state = CONTENT_PACKAGE_STATE_FAILED;
        snapshot.last_error = CONTENT_PACKAGE_ERR_NOT_INITIALIZED;
        return snapshot;
    }
    snapshot.state = runtime->state;
    snapshot.last_error = runtime->last_error;
    snapshot.catalog_revision = runtime->catalog_revision;
    snapshot.ready_packages = runtime->ready_packages;
    snapshot.pending_downloads = runtime->pending_downloads;
    memcpy(
        snapshot.active_package_id,
        runtime->active_package_id,
        sizeof(snapshot.active_package_id)
    );
    snapshot.active_received_bytes = runtime->active_received_bytes;
    snapshot.active_total_bytes = runtime->active_total_bytes;
    return snapshot;
}
