#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"
#include "esp_err.h"
#include "module_registry.h"
#include "playback_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief File system mount point owned by the downloader. */
#define CONTENT_DOWNLOADER_MOUNT_POINT "/content"

/** @brief Partition label that stores downloaded packages. */
#define CONTENT_DOWNLOADER_PARTITION_LABEL "content"

/** @brief Maximum package byte size accepted into the partition. */
#define CONTENT_DOWNLOADER_MAX_PACKAGE_BYTES (768u * 1024u)

/** @brief Maximum PCM frames submitted in one ambient playback item. */
#define CONTENT_DOWNLOADER_PLAYBACK_FRAMES_PER_ITEM \
    PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM

/** @brief Stable content playback error codes. */
typedef enum {
    CONTENT_PLAYBACK_OK = 0,
    CONTENT_PLAYBACK_ERR_NOT_INITIALIZED,
    CONTENT_PLAYBACK_ERR_INVALID_ARGUMENT,
    CONTENT_PLAYBACK_ERR_PACKAGE_NOT_READY,
    CONTENT_PLAYBACK_ERR_STORAGE,
    CONTENT_PLAYBACK_ERR_QUEUE_FULL,
    CONTENT_PLAYBACK_ERR_CANCELED,
} content_playback_error_t;

/** @brief Stable download error codes for diagnostics and UI copy. */
typedef enum {
    CONTENT_DOWNLOADER_OK = 0,
    CONTENT_DOWNLOADER_ERR_NOT_INITIALIZED,
    CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT,
    CONTENT_DOWNLOADER_ERR_OFFLINE,
    CONTENT_DOWNLOADER_ERR_TRANSPORT,
    CONTENT_DOWNLOADER_ERR_HTTP_STATUS,
    CONTENT_DOWNLOADER_ERR_TOO_LARGE,
    CONTENT_DOWNLOADER_ERR_STORAGE,
    CONTENT_DOWNLOADER_ERR_HASH_MISMATCH,
    CONTENT_DOWNLOADER_ERR_CANCELED,
    CONTENT_DOWNLOADER_ERR_RETRY_EXHAUSTED,
} content_downloader_error_t;

/** @brief Transfer status shared with the package manager and device UI. */
typedef enum {
    CONTENT_DOWNLOAD_STATUS_IDLE = 0,
    CONTENT_DOWNLOAD_STATUS_WAITING,
    CONTENT_DOWNLOAD_STATUS_DOWNLOADING,
    CONTENT_DOWNLOAD_STATUS_VERIFYING,
    CONTENT_DOWNLOAD_STATUS_COMPLETE,
    CONTENT_DOWNLOAD_STATUS_FAILED,
    CONTENT_DOWNLOAD_STATUS_CANCELED,
} content_download_status_t;

/** @brief Cancel and retry controls used by the package manager. */
typedef enum {
    CONTENT_DOWNLOAD_CONTROL_NONE = 0,
    CONTENT_DOWNLOAD_CONTROL_CANCEL,
    CONTENT_DOWNLOAD_CONTROL_RETRY,
} content_download_control_t;

/** @brief One immutable transfer request. */
typedef struct {
    char package_id[CONTENT_LIBRARY_ID_SIZE];
    char download_url[512];
    char sha256[65];
    uint64_t size_bytes;
} content_download_request_t;

/** @brief Bounded transfer progress. */
typedef struct {
    content_download_status_t status;
    char package_id[CONTENT_LIBRARY_ID_SIZE];
    uint64_t received_bytes;
    uint64_t total_bytes;
    int attempt;
    content_downloader_error_t last_error;
} content_download_progress_t;

/** @brief Bounded content playback progress for the device UI. */
typedef struct {
    bool playing;
    char package_id[CONTENT_LIBRARY_ID_SIZE];
    uint64_t queued_frames;
    uint64_t total_frames;
} content_playback_progress_t;

/** @brief Initialize the content partition and worker state. */
esp_err_t content_downloader_init(void);

/** @brief Return true when the downloader can accept requests. */
bool content_downloader_is_ready(void);

/**
 * @brief Queue one package for serial download.
 *
 * The downloader performs one transfer at a time. A duplicate package already
 * waiting or downloading is rejected so the same file is never written twice.
 */
esp_err_t content_downloader_enqueue(
    const content_download_request_t *request
);

/** @brief Request cancellation of the active or waiting transfer. */
esp_err_t content_downloader_cancel(const char *package_id);

/** @brief Retry the last failed transfer using the same request. */
esp_err_t content_downloader_retry(const char *package_id);

/** @brief Copy the current progress snapshot. */
esp_err_t content_downloader_get_progress(
    const char *package_id,
    content_download_progress_t *progress_out
);

/**
 * @brief Return the local path for a ready package.
 *
 * The path is valid only after the library entry reports
 * CONTENT_LIBRARY_STATE_READY.
 */
esp_err_t content_downloader_get_local_path(
    const char *package_id,
    char *output,
    size_t output_size
);

/** @brief Delete one local package file and its temporary download. */
esp_err_t content_downloader_delete(const char *package_id);

/**
 * @brief Start playback of a locally downloaded package.
 *
 * The package must be READY in content_library. Playback uses the ambient
 * priority and submits at most 32 PCM frames per queue item. Starting a second
 * package replaces the first at a chunk boundary.
 */
esp_err_t content_playback_start(const char *package_id);

/** @brief Stop active content playback without clearing priority audio. */
esp_err_t content_playback_stop(void);

/** @brief Copy the current content playback progress. */
esp_err_t content_playback_get_progress(
    content_playback_progress_t *progress_out
);

/** @brief Return the stable string for one download error code. */
const char *content_downloader_error_name(content_downloader_error_t error);

/** @brief Return the stable string for one download status. */
const char *content_download_status_name(content_download_status_t status);

/** @brief Return the stable string for one content playback error code. */
const char *content_playback_error_name(content_playback_error_t error);

/** @brief Return the removable-module descriptor for content_downloader. */
const module_descriptor_t *content_downloader_module_descriptor(void);

#ifdef __cplusplus
}
#endif
