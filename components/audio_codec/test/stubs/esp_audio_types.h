#pragma once

/* Minimal host copy of the esp_audio_codec error contract. The values match the
   managed component so an error mapping regression is caught here. */
typedef enum {
    ESP_AUDIO_ERR_CONTINUE = 1,
    ESP_AUDIO_ERR_OK = 0,
    ESP_AUDIO_ERR_FAIL = -1,
    ESP_AUDIO_ERR_MEM_LACK = -2,
    ESP_AUDIO_ERR_DATA_LACK = -3,
    ESP_AUDIO_ERR_HEADER_PARSE = -4,
    ESP_AUDIO_ERR_INVALID_PARAMETER = -5,
    ESP_AUDIO_ERR_ALREADY_EXIST = -6,
    ESP_AUDIO_ERR_NOT_SUPPORT = -7,
    ESP_AUDIO_ERR_BUFF_NOT_ENOUGH = -8,
    ESP_AUDIO_ERR_NOT_FOUND = -9,
} esp_audio_err_t;
