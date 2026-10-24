// OV2640 sensor driver glue for the camera capture lifecycle.
//
// The lifecycle component decides whether a capture is allowed; this file only
// talks to the sensor. It stays a separate translation unit so a profile that
// compiles the camera out never pulls the driver's headers or symbols in, and
// so the consent decision cannot be re-implemented here by accident.

#include "camera_capture_sensor.h"

#if CAMERA_CAPTURE_HAS_SENSOR

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

extern "C" {
#include "esp_camera.h"
}

namespace {

constexpr char kTag[] = "camera_capture";

// The driver is not re-entrant: one capture owns the sensor at a time.
SemaphoreHandle_t g_sensor_lock = nullptr;
bool g_sensor_ready = false;

camera_config_t camera_capture_sensor_config() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CONFIG_CAMERA_PIN_D0;
    config.pin_d1 = CONFIG_CAMERA_PIN_D1;
    config.pin_d2 = CONFIG_CAMERA_PIN_D2;
    config.pin_d3 = CONFIG_CAMERA_PIN_D3;
    config.pin_d4 = CONFIG_CAMERA_PIN_D4;
    config.pin_d5 = CONFIG_CAMERA_PIN_D5;
    config.pin_d6 = CONFIG_CAMERA_PIN_D6;
    config.pin_d7 = CONFIG_CAMERA_PIN_D7;
    config.pin_xclk = CONFIG_CAMERA_PIN_XCLK;
    config.pin_pclk = CONFIG_CAMERA_PIN_PCLK;
    config.pin_vsync = CONFIG_CAMERA_PIN_VSYNC;
    config.pin_href = CONFIG_CAMERA_PIN_HREF;
    config.pin_sccb_sda = CONFIG_CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CONFIG_CAMERA_PIN_SIOC;
    config.pin_pwdn = CONFIG_CAMERA_PIN_PWDN;
    config.pin_reset = CONFIG_CAMERA_PIN_RESET;
    config.xclk_freq_hz = CONFIG_CAMERA_XCLK_FREQUENCY_HZ;
    // A still is delivered as JPEG so one frame is one self-contained image the
    // gateway can hand to the parent app without a decoder on the device.
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = CONFIG_CAMERA_FRAME_QUALITY;
    // Two buffers let a snapshot overlap the previous frame's release.
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    return config;
}

bool camera_capture_sensor_lock() {
    if (g_sensor_lock == nullptr) {
        g_sensor_lock = xSemaphoreCreateMutex();
        if (g_sensor_lock == nullptr) {
            return false;
        }
    }
    return xSemaphoreTake(g_sensor_lock, pdMS_TO_TICKS(3000)) == pdTRUE;
}

void camera_capture_sensor_unlock() {
    if (g_sensor_lock != nullptr) {
        xSemaphoreGive(g_sensor_lock);
    }
}

}  // namespace

esp_err_t camera_capture_sensor_init(void) {
    if (g_sensor_ready) {
        return ESP_OK;
    }
    if (!camera_capture_sensor_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    camera_config_t config = camera_capture_sensor_config();
    const esp_err_t result = esp_camera_init(&config);
    camera_capture_sensor_unlock();
    if (result != ESP_OK) {
        // Only the driver's own status is logged. No frame and no pin value is
        // ever logged, because a pin map is board information.
        ESP_LOGW(kTag, "sensor init failed: %s", esp_err_to_name(result));
        return result;
    }
    g_sensor_ready = true;
    return ESP_OK;
}

bool camera_capture_sensor_is_ready(void) {
    return g_sensor_ready;
}

camera_capture_sensor_frame_t camera_capture_sensor_read(
    uint8_t *buffer,
    size_t buffer_size
) {
    camera_capture_sensor_frame_t frame = {};
    if (!g_sensor_ready || buffer == nullptr || buffer_size == 0) {
        return frame;
    }
    if (!camera_capture_sensor_lock()) {
        return frame;
    }
    camera_fb_t *snapshot = esp_camera_fb_get();
    if (snapshot == nullptr) {
        camera_capture_sensor_unlock();
        return frame;
    }
    if (snapshot->format == PIXFORMAT_JPEG && snapshot->len > 0 &&
        snapshot->len <= buffer_size) {
        memcpy(buffer, snapshot->buf, snapshot->len);
        frame.width = snapshot->width;
        frame.height = snapshot->height;
        frame.length = snapshot->len;
        frame.ok = true;
    }
    // The buffer belongs to the driver: returning it before leaving is what lets
    // the next frame be captured at all.
    esp_camera_fb_return(snapshot);
    camera_capture_sensor_unlock();
    return frame;
}

void camera_capture_sensor_deinit(void) {
    if (!g_sensor_ready) {
        return;
    }
    if (camera_capture_sensor_lock()) {
        esp_camera_deinit();
        g_sensor_ready = false;
        camera_capture_sensor_unlock();
    }
}

#endif  // CAMERA_CAPTURE_HAS_SENSOR
