/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
//#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_system.h"
#include "esp_check.h"
#include "example_config.h"

#include "eth.h"
#define CONFIG_EXAMPLE_BSP 1
#define CONFIG_IDF_TARGET_ESP32P4 1 
#undef CONFIG_EXAMPLE_MODE_MUSIC

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

//   ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, EXAMPLE_MIC_GAIN), TAG, "set es8311 microphone gain failed");
static const char *TAG = "i2s_es8311";
static const char err_reason[][30] = {"input param is invalid",
                                      "operation timeout"
                                     };

static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;
static bool s_play_opened;
static bool s_record_opened;


/* Import music file as buffer */
#if CONFIG_EXAMPLE_MODE_MUSIC
extern const uint8_t music_pcm_start[] asm("_binary_canon_pcm_start");
extern const uint8_t music_pcm_end[]   asm("_binary_canon_pcm_end");
#endif



#if CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4
static esp_err_t p4_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    ESP_RETURN_ON_FALSE(s_play_dev || s_record_dev, ESP_ERR_INVALID_STATE, TAG, "codec is not initialized");

    esp_err_t ret = ESP_OK;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (s_play_opened) {
        ret |= esp_codec_dev_close(s_play_dev);
        s_play_opened = false;
    }
    if (s_record_opened) {
        ret |= esp_codec_dev_close(s_record_dev);
        s_record_opened = false;
    }
    if (s_play_dev) {
        ret |= esp_codec_dev_open(s_play_dev, &fs);
        if (ret == ESP_OK) {
            s_play_opened = true;
        }
    }
    if (s_record_dev) {
        ret |= esp_codec_dev_set_in_gain(s_record_dev, 24.0);
        ret |= esp_codec_dev_open(s_record_dev, &fs);
        if (ret == ESP_OK) {
            s_record_opened = true;
        }
    }
    return ret;
}

static esp_err_t p4_codec_volume_set(int volume)
{
    ESP_RETURN_ON_FALSE(s_play_dev, ESP_ERR_INVALID_STATE, TAG, "speaker codec is not initialized");
    return esp_codec_dev_set_out_vol(s_play_dev, volume);
}
#endif

static esp_err_t es8311_codec_init(void)
{
    s_play_dev = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_play_dev, ESP_FAIL, TAG, "init ESP32-P4 speaker codec failed");
    s_record_dev = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_record_dev, ESP_FAIL, TAG, "init ESP32-P4 microphone codec failed");
    ESP_RETURN_ON_ERROR(p4_codec_set_fs(EXAMPLE_SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO), TAG, "set ESP32-P4 BSP codec format failed");
    ESP_RETURN_ON_ERROR(p4_codec_volume_set(EXAMPLE_VOICE_VOLUME), TAG, "set ESP32-P4 BSP codec volume failed");
    return ESP_OK;
}

#if CONFIG_EXAMPLE_MODE_MUSIC
static void i2s_music(void *args)
{
    esp_err_t ret = ESP_OK;
    size_t bytes_write = 0;
    uint8_t *data_ptr = (uint8_t *)music_pcm_start;
    const size_t music_size = music_pcm_end - music_pcm_start;

#if !(CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4)
    ESP_ERROR_CHECK(i2s_channel_disable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_preload_data(tx_handle, data_ptr, music_size, &bytes_write));
    data_ptr += bytes_write;
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
#endif

    while (1) {
#if CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4
        data_ptr = (uint8_t *)music_pcm_start;
        ret = esp_codec_dev_write(s_play_dev, data_ptr, music_size);
        bytes_write = (ret == ESP_OK) ? music_size : 0;
#else
        ret = i2s_channel_write(tx_handle, data_ptr, music_pcm_end - data_ptr, &bytes_write, portMAX_DELAY);
#endif
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[music] i2s write failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
            abort();
        }
        if (bytes_write > 0) {
            ESP_LOGI(TAG, "[music] i2s music played, %u bytes are written.", (unsigned int)bytes_write);
        } else {
            ESP_LOGE(TAG, "[music] i2s music play failed.");
            abort();
        }
        data_ptr = (uint8_t *)music_pcm_start;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

#else
static void i2s_echo(void *args)
{
    int *mic_data = malloc(EXAMPLE_RECV_BUF_SIZE);
    if (!mic_data) {
        ESP_LOGE(TAG, "[echo] No memory for read data buffer");
        abort();
    }
    esp_err_t ret = ESP_OK;
    size_t bytes_read = 0;
    size_t bytes_write = 0;
    ESP_LOGI(TAG, "[echo] Echo start");


 // ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, EXAMPLE_MIC_GAIN), TAG, "set es8311 microphone gain failed");

    while (1) {
        memset(mic_data, 0, EXAMPLE_RECV_BUF_SIZE);
#if CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4
        ret = esp_codec_dev_read(s_record_dev, mic_data, EXAMPLE_RECV_BUF_SIZE);
        bytes_read = (ret == ESP_OK) ? EXAMPLE_RECV_BUF_SIZE : 0;
#else
        ret = i2s_channel_read(rx_handle, mic_data, EXAMPLE_RECV_BUF_SIZE, &bytes_read, 1000);
#endif
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[echo] i2s read failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
            abort();
        }
#if CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4
        ret = esp_codec_dev_write(s_play_dev, mic_data, EXAMPLE_RECV_BUF_SIZE);
        bytes_write = (ret == ESP_OK) ? EXAMPLE_RECV_BUF_SIZE : 0;
#else
        ret = i2s_channel_write(tx_handle, mic_data, EXAMPLE_RECV_BUF_SIZE, &bytes_write, 1000);
#endif
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[echo] i2s write failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
            abort();
        }
        if (bytes_read != bytes_write) {
            ESP_LOGW(TAG, "[echo] %u bytes read but only %u bytes are written", (unsigned int)bytes_read, (unsigned int)bytes_write);
        }
    }
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    printf("i2s es8311 codec example start\n-----------------------------\n");
    initETH(); 
    if (es8311_codec_init() != ESP_OK) {
        ESP_LOGE(TAG, "es8311 codec init failed");
        abort();
    } else {
        ESP_LOGI(TAG, "es8311 codec init success");
    }
#if CONFIG_EXAMPLE_MODE_MUSIC
    xTaskCreate(i2s_music, "i2s_music", 4096, NULL, 5, NULL);
#else
    xTaskCreate(i2s_echo, "i2s_echo", 8192, NULL, 5, NULL);
#endif
}
