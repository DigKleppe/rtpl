/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

// ontvanger in testAudioRTP /mnt/linuxData/projecten/intercom

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "example_config.h"
#include "network_out.h"

#include "udpServer.h"

#define RINGBUFSIZE 44100

#include "eth.h"
#define CONFIG_EXAMPLE_BSP 1
#define CONFIG_IDF_TARGET_ESP32P4 1
#undef CONFIG_EXAMPLE_MODE_MUSIC

#define MIC_GAIN 42 // max 42 db ES8311_ADC_REG16
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

#define UDPRECEIVEPORT 5006
#define UDPMAXLEN 2048

static const char *TAG = "receiver";
//static const char err_reason[][30] = {"input param is invalid", "operation timeout"};

static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;

extern volatile bool gotIP;
RingbufHandle_t pcmBuffer;
int micBytes, udpBytes;

static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;
static bool s_play_opened;
static bool s_record_opened;

int es8311_set_ADCvolume(const audio_codec_if_t *h, uint8_t val);

static esp_err_t p4_codec_volume_set(int volume) {
	ESP_RETURN_ON_FALSE(s_play_dev, ESP_ERR_INVALID_STATE, TAG, "speaker codec is not initialized");
	return esp_codec_dev_set_out_vol(s_play_dev, volume);
}
#if CONFIG_EXAMPLE_BSP && CONFIG_IDF_TARGET_ESP32P4
static esp_err_t p4_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch) {
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
#endif

static esp_err_t es8311_codec_init(void) {
	s_play_dev = bsp_audio_codec_speaker_init();
	ESP_RETURN_ON_FALSE(s_play_dev, ESP_FAIL, TAG, "init ESP32-P4 speaker codec failed");
	s_record_dev = bsp_audio_codec_microphone_init();
	ESP_RETURN_ON_FALSE(s_record_dev, ESP_FAIL, TAG, "init ESP32-P4 microphone codec failed");
	//	ESP_RETURN_ON_ERROR(p4_codec_set_fs(EXAMPLE_SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO), TAG, "set ESP32-P4 BSP codec format failed");
	ESP_RETURN_ON_ERROR(p4_codec_set_fs(EXAMPLE_SAMPLE_RATE, 16, I2S_SLOT_MODE_MONO), TAG, "set ESP32-P4 BSP codec format failed");

	ESP_RETURN_ON_ERROR(p4_codec_volume_set(EXAMPLE_VOICE_VOLUME), TAG, "set ESP32-P4 BSP codec volume failed");
	// audio_codec_if_t h = s_record_dev.
	// es8311_set_ADCvolume(s_record_dev, 0xFF); // max

	return ESP_OK;
}

void timerTask(void *p) {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	int lastMicBytes = 0;
	while (1) {
		ESP_LOGI(TAG, "mic received %d  %d  send: %d", micBytes, micBytes - lastMicBytes, udpBytes);
		lastMicBytes = micBytes;
		vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_PERIOD_MS);
	};
}

TaskHandle_t udpServerTaskh;

void app_main(void) {
	printf("start\n-----------------------------\n");

	initETH();

	if (es8311_codec_init() != ESP_OK) {
		ESP_LOGE(TAG, "es8311 codec init failed");
		abort();
	} else {
		ESP_LOGI(TAG, "es8311 codec init success");
	}

	pcmBuffer = xRingbufferCreate(RINGBUFSIZE, RINGBUF_TYPE_BYTEBUF);
	if (pcmBuffer == NULL) {
		printf("Failed to create ring buffer\n");
	} else {
		const udpTaskParams_t udpTaskParams = {.port = UDPRECEIVEPORT, .ringBuf = pcmBuffer};
		xTaskCreate(udpServerTask, "udpServerTask", 1024 * 2, (void *)&udpTaskParams, 5, &udpServerTaskh);
	}

	//	xTaskCreate(timerTask, "timer", 2000, NULL, 5, NULL);
	//	xTaskCreate(sendmusic, "rtp_sender", 8192, NULL, 5, NULL);
	// generateTestTone();
}
