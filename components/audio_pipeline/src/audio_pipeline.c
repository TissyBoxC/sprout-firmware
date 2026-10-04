#include "audio_pipeline.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *const TAG = "audio_pipeline";

// One I2S controller serves both directions so the microphone and the speaker
// share the same clock domain. The handles are only touched from the audio
// tasks, guarded by one mutex for enable/disable transitions.
static i2s_chan_handle_t audio_pipeline_tx_handle;
static i2s_chan_handle_t audio_pipeline_rx_handle;
static SemaphoreHandle_t audio_pipeline_mutex;
static bool audio_pipeline_ready;
static bool audio_pipeline_is_capture_active;
static bool audio_pipeline_is_playback_active;
static audio_pipeline_capture_owner_t audio_pipeline_capture_owner =
    AUDIO_PIPELINE_CAPTURE_OWNER_NONE;

// The reference sink is copied under a critical section instead of a mutex so a
// sink can be registered while the playback task runs, without holding a lock
// across the callback body the caller owns.
static portMUX_TYPE audio_pipeline_reference_lock =
    portMUX_INITIALIZER_UNLOCKED;
static audio_pipeline_reference_sink_t audio_pipeline_reference_sink;
static void *audio_pipeline_reference_context;

static audio_pipeline_snapshot_t audio_pipeline_snapshot = {
    .state = AUDIO_PIPELINE_STATE_STOPPED,
};

static bool audio_pipeline_capture_owner_is_valid(
    audio_pipeline_capture_owner_t owner
) {
    return owner == AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE ||
        owner == AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT;
}

static void audio_pipeline_update_state_locked(void) {
    if (audio_pipeline_is_capture_active) {
        audio_pipeline_snapshot.state = AUDIO_PIPELINE_STATE_CAPTURING;
    } else if (audio_pipeline_is_playback_active) {
        audio_pipeline_snapshot.state = AUDIO_PIPELINE_STATE_PLAYING;
    } else {
        audio_pipeline_snapshot.state = AUDIO_PIPELINE_STATE_STOPPED;
    }
}

static i2s_std_gpio_config_t audio_pipeline_make_gpio_config(void) {
    i2s_std_gpio_config_t gpio_config = {
        .mclk = CONFIG_AUDIO_PIPELINE_MCLK_GPIO >= 0
            ? (gpio_num_t)CONFIG_AUDIO_PIPELINE_MCLK_GPIO
            : I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)CONFIG_AUDIO_PIPELINE_BCLK_GPIO,
        .ws = (gpio_num_t)CONFIG_AUDIO_PIPELINE_WS_GPIO,
#if CONFIG_AUDIO_PIPELINE_INPUT_ENABLED
        .din = (gpio_num_t)CONFIG_AUDIO_PIPELINE_DATA_IN_GPIO,
#else
        .din = I2S_GPIO_UNUSED,
#endif
#if CONFIG_AUDIO_PIPELINE_OUTPUT_ENABLED
        .dout = (gpio_num_t)CONFIG_AUDIO_PIPELINE_DATA_OUT_GPIO,
#else
        .dout = I2S_GPIO_UNUSED,
#endif
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };
    return gpio_config;
}

static i2s_std_config_t audio_pipeline_make_std_config(void) {
    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CODEC_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = audio_pipeline_make_gpio_config(),
    };
    // The microphone is mono, so the mono slot must be placed in the left
    // channel to match the codec profile the gateway expects.
    std_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    return std_config;
}

esp_err_t audio_pipeline_init(void) {
    if (audio_pipeline_ready) {
        return ESP_OK;
    }
    if (!audio_codec_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (audio_pipeline_mutex == NULL) {
        audio_pipeline_mutex = xSemaphoreCreateMutex();
        if (audio_pipeline_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const i2s_chan_config_t channel_config = {
        .id = CONFIG_AUDIO_PIPELINE_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = CONFIG_AUDIO_PIPELINE_DMA_DESCRIPTOR_COUNT,
        .dma_frame_num = CONFIG_AUDIO_PIPELINE_DMA_FRAME_COUNT,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .allow_pd = false,
        .intr_priority = 0,
    };

    esp_err_t result = i2s_new_channel(
        &channel_config,
#if CONFIG_AUDIO_PIPELINE_OUTPUT_ENABLED
        &audio_pipeline_tx_handle,
#else
        NULL,
#endif
#if CONFIG_AUDIO_PIPELINE_INPUT_ENABLED
        &audio_pipeline_rx_handle
#else
        NULL
#endif
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "i2s channel allocation failed: %s", esp_err_to_name(result));
        return result;
    }

    const i2s_std_config_t std_config = audio_pipeline_make_std_config();

    if (audio_pipeline_rx_handle != NULL) {
        result = i2s_channel_init_std_mode(audio_pipeline_rx_handle, &std_config);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "i2s rx init failed: %s", esp_err_to_name(result));
            i2s_del_channel(audio_pipeline_rx_handle);
            audio_pipeline_rx_handle = NULL;
            if (audio_pipeline_tx_handle != NULL) {
                i2s_del_channel(audio_pipeline_tx_handle);
                audio_pipeline_tx_handle = NULL;
            }
            return result;
        }
    }

    if (audio_pipeline_tx_handle != NULL) {
        result = i2s_channel_init_std_mode(audio_pipeline_tx_handle, &std_config);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "i2s tx init failed: %s", esp_err_to_name(result));
            if (audio_pipeline_rx_handle != NULL) {
                i2s_del_channel(audio_pipeline_rx_handle);
                audio_pipeline_rx_handle = NULL;
            }
            i2s_del_channel(audio_pipeline_tx_handle);
            audio_pipeline_tx_handle = NULL;
            return result;
        }
    }

    audio_pipeline_ready = true;
    audio_pipeline_snapshot.is_microphone_enabled =
        audio_pipeline_rx_handle != NULL;
    audio_pipeline_snapshot.is_speaker_enabled =
        audio_pipeline_tx_handle != NULL;
    return ESP_OK;
}

bool audio_pipeline_is_ready(void) {
    return audio_pipeline_ready;
}

audio_codec_error_t audio_pipeline_capture_frame(
    audio_pipeline_capture_owner_t owner,
    audio_codec_pcm_frame_t *frame_out
) {
    if (!audio_pipeline_ready || audio_pipeline_rx_handle == NULL) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (frame_out == NULL) {
        return AUDIO_CODEC_ERR_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    // Keep the owner check and the I2S read under the same lock. Release
    // therefore cannot disable the channel or hand the lease to another owner
    // while this 20 ms frame is being read.
    if (!audio_pipeline_capture_owner_is_valid(owner) ||
        audio_pipeline_capture_owner != owner ||
        !audio_pipeline_is_capture_active) {
        xSemaphoreGive(audio_pipeline_mutex);
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }

    size_t bytes_read = 0;
    const esp_err_t result = i2s_channel_read(
        audio_pipeline_rx_handle,
        frame_out->pcm,
        sizeof(frame_out->pcm),
        &bytes_read,
        CONFIG_AUDIO_PIPELINE_CAPTURE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        audio_pipeline_snapshot.capture_errors++;
        xSemaphoreGive(audio_pipeline_mutex);
        return AUDIO_CODEC_ERR_DECODE_FAILED;
    }
    if (bytes_read != sizeof(frame_out->pcm)) {
        // A partial frame would desynchronise the 20 ms cadence, so it is
        // reported as a failure and never forwarded as valid audio.
        audio_pipeline_snapshot.capture_errors++;
        xSemaphoreGive(audio_pipeline_mutex);
        return AUDIO_CODEC_ERR_INVALID_PCM_SIZE;
    }

    frame_out->pcm_size = bytes_read;
    audio_pipeline_snapshot.captured_frames++;
    xSemaphoreGive(audio_pipeline_mutex);
    return AUDIO_CODEC_OK;
}

audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
) {
    if (!audio_pipeline_ready || audio_pipeline_tx_handle == NULL) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }
    if (frame == NULL || frame->pcm_size != sizeof(frame->pcm)) {
        return AUDIO_CODEC_ERR_INVALID_PCM_SIZE;
    }
    if (!audio_pipeline_is_playback_active) {
        return AUDIO_CODEC_ERR_NOT_INITIALIZED;
    }

    size_t bytes_written = 0;
    const esp_err_t result = i2s_channel_write(
        audio_pipeline_tx_handle,
        frame->pcm,
        sizeof(frame->pcm),
        &bytes_written,
        CONFIG_AUDIO_PIPELINE_PLAYBACK_TIMEOUT_MS
    );
    if (result != ESP_OK || bytes_written != sizeof(frame->pcm)) {
        audio_pipeline_snapshot.playback_errors++;
        return AUDIO_CODEC_ERR_ENCODE_FAILED;
    }

    audio_pipeline_snapshot.played_frames++;

    // Publish the signal only after it reached I2S: that is the waveform the
    // microphone will actually hear and therefore the correct echo reference.
    audio_pipeline_reference_sink_t reference_sink;
    void *reference_context;
    portENTER_CRITICAL(&audio_pipeline_reference_lock);
    reference_sink = audio_pipeline_reference_sink;
    reference_context = audio_pipeline_reference_context;
    portEXIT_CRITICAL(&audio_pipeline_reference_lock);
    if (reference_sink != NULL) {
        reference_sink(
            frame->pcm,
            AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT,
            reference_context
        );
    }
    return AUDIO_CODEC_OK;
}

esp_err_t audio_pipeline_set_capturing(bool is_capturing) {
    if (audio_pipeline_rx_handle == NULL) {
        return is_capturing ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (is_capturing && !audio_pipeline_is_capture_active) {
        result = i2s_channel_enable(audio_pipeline_rx_handle);
        if (result == ESP_OK) {
            audio_pipeline_is_capture_active = true;
            audio_pipeline_update_state_locked();
        }
    } else if (!is_capturing && audio_pipeline_is_capture_active) {
        // A lease is the authority for capture; the legacy Boolean gate must
        // not be able to strand an active owner without a reader.
        if (audio_pipeline_capture_owner !=
            AUDIO_PIPELINE_CAPTURE_OWNER_NONE) {
            result = ESP_ERR_INVALID_STATE;
        } else {
            result = i2s_channel_disable(audio_pipeline_rx_handle);
            if (result == ESP_OK) {
                audio_pipeline_is_capture_active = false;
                audio_pipeline_update_state_locked();
            }
        }
    }

    xSemaphoreGive(audio_pipeline_mutex);
    return result;
}

esp_err_t audio_pipeline_capture_acquire(
    audio_pipeline_capture_owner_t owner
) {
    if (audio_pipeline_rx_handle == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!audio_pipeline_capture_owner_is_valid(owner)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (audio_pipeline_capture_owner !=
        AUDIO_PIPELINE_CAPTURE_OWNER_NONE &&
        audio_pipeline_capture_owner != owner) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!audio_pipeline_is_capture_active) {
        result = i2s_channel_enable(audio_pipeline_rx_handle);
        if (result == ESP_OK) {
            audio_pipeline_is_capture_active = true;
        }
    } else {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        audio_pipeline_capture_owner = owner;
        audio_pipeline_update_state_locked();
    }

    xSemaphoreGive(audio_pipeline_mutex);
    return result;
}

esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
) {
    if (audio_pipeline_rx_handle == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!audio_pipeline_capture_owner_is_valid(owner)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (audio_pipeline_capture_owner != owner) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!audio_pipeline_is_capture_active) {
        audio_pipeline_capture_owner = AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
        audio_pipeline_update_state_locked();
    } else {
        result = i2s_channel_disable(audio_pipeline_rx_handle);
        if (result == ESP_OK) {
            audio_pipeline_capture_owner = AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
            audio_pipeline_is_capture_active = false;
            audio_pipeline_update_state_locked();
        }
    }

    xSemaphoreGive(audio_pipeline_mutex);
    return result;
}

audio_pipeline_capture_owner_t audio_pipeline_capture_get_owner(void) {
    if (audio_pipeline_mutex == NULL) {
        return AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return AUDIO_PIPELINE_CAPTURE_OWNER_NONE;
    }
    const audio_pipeline_capture_owner_t owner =
        audio_pipeline_capture_owner;
    xSemaphoreGive(audio_pipeline_mutex);
    return owner;
}

esp_err_t audio_pipeline_set_playing(bool is_playing) {
    if (audio_pipeline_tx_handle == NULL) {
        return is_playing ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
    }
    if (xSemaphoreTake(audio_pipeline_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (is_playing && !audio_pipeline_is_playback_active) {
        result = i2s_channel_enable(audio_pipeline_tx_handle);
        if (result == ESP_OK) {
            audio_pipeline_is_playback_active = true;
            audio_pipeline_update_state_locked();
        }
    } else if (!is_playing && audio_pipeline_is_playback_active) {
        result = i2s_channel_disable(audio_pipeline_tx_handle);
        if (result == ESP_OK) {
            audio_pipeline_is_playback_active = false;
            audio_pipeline_update_state_locked();
        }
    }

    xSemaphoreGive(audio_pipeline_mutex);
    return result;
}

bool audio_pipeline_is_capturing(void) {
    return audio_pipeline_is_capture_active;
}

bool audio_pipeline_is_playing(void) {
    return audio_pipeline_is_playback_active;
}

audio_pipeline_snapshot_t audio_pipeline_get_snapshot(void) {
    return audio_pipeline_snapshot;
}

esp_err_t audio_pipeline_set_reference_sink(
    audio_pipeline_reference_sink_t sink,
    void *context
) {
    portENTER_CRITICAL(&audio_pipeline_reference_lock);
    audio_pipeline_reference_sink = sink;
    audio_pipeline_reference_context = sink == NULL ? NULL : context;
    portEXIT_CRITICAL(&audio_pipeline_reference_lock);
    return ESP_OK;
}

const module_descriptor_t *audio_pipeline_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "audio_pipeline",
        .version = "1.0.0",
        .initialize = audio_pipeline_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
