#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_package_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTENT_PACKAGE_COMMAND_NONE = 0,
    CONTENT_PACKAGE_COMMAND_SYNC,
    CONTENT_PACKAGE_COMMAND_PAUSE,
    CONTENT_PACKAGE_COMMAND_RESUME,
    CONTENT_PACKAGE_COMMAND_STOP,
} content_package_command_t;

/** @brief Pure editor state for the package synchroniser. */
typedef struct {
    content_package_state_t state;
    content_package_error_t last_error;
    content_package_command_t command;
    bool paused;
    bool sync_requested;
    bool running;
    int64_t catalog_revision;
    size_t ready_packages;
    size_t pending_downloads;
    char active_package_id[CONTENT_LIBRARY_ID_SIZE];
    uint64_t active_received_bytes;
    uint64_t active_total_bytes;
} content_package_runtime_t;

void content_package_runtime_init(content_package_runtime_t *runtime);

void content_package_runtime_request_sync(
    content_package_runtime_t *runtime
);

void content_package_runtime_request_pause(
    content_package_runtime_t *runtime
);

void content_package_runtime_request_resume(
    content_package_runtime_t *runtime
);

void content_package_runtime_mark_manifest_started(
    content_package_runtime_t *runtime
);

void content_package_runtime_mark_manifest_succeeded(
    content_package_runtime_t *runtime,
    size_t ready_packages,
    size_t pending_downloads
);

void content_package_runtime_mark_catalog_revision(
    content_package_runtime_t *runtime,
    int64_t catalog_revision
);

void content_package_runtime_mark_download(
    content_package_runtime_t *runtime,
    const char *package_id,
    uint64_t received_bytes,
    uint64_t total_bytes
);

void content_package_runtime_mark_failed(
    content_package_runtime_t *runtime,
    content_package_error_t error
);

void content_package_runtime_mark_complete(
    content_package_runtime_t *runtime
);

content_package_snapshot_t content_package_runtime_snapshot(
    const content_package_runtime_t *runtime
);

#ifdef __cplusplus
}
#endif
