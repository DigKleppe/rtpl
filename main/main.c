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

#include "esp_check.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "network_out.h"

#define RINGBUFSIZE 44100

#define S3BOARD 1

#ifndef S3BOARD
#include "eth.h"
#define CONFIG_EXAMPLE_BSP 1
#define CONFIG_IDF_TARGET_ESP32P4 1
#include "driver/i2s_std.h"
#include "example_config.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;
int es8311_set_ADCvolume(const audio_codec_if_t *h, uint8_t val);
static const char err_reason[][30] = {"input param is invalid", "operation timeout"};

#else
void initWifi(void);

#define EXAMPLE_SAMPLE_RATE 16000
#endif

#undef CONFIG_EXAMPLE_MODE_MUSIC

#define MIC_GAIN 42 // max 42 db ES8311_ADC_REG16


static const char *TAG = "i2s_es8311";



static bool s_play_opened;
static bool s_record_opened;

extern volatile bool gotIP;
RingbufHandle_t pcmBuffer;
int micBytes, udpBytes;

/* Import music file as buffer */
extern const uint8_t music_pcm_start[] asm("_binary_canon_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_canon_pcm_end");



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
		ret |= esp_codec_dev_set_in_gain(s_record_dev, MIC_GAIN);
		ret |= esp_codec_dev_open(s_record_dev, &fs);
		if (ret == ESP_OK) {
			s_record_opened = true;
		}
	}
	return ret;
}

static esp_err_t p4_codec_volume_set(int volume) {
	ESP_RETURN_ON_FALSE(s_play_dev, ESP_ERR_INVALID_STATE, TAG, "speaker codec is not initialized");
	return esp_codec_dev_set_out_vol(s_play_dev, volume);
}


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
#endif



#include "math.h"
void generateTestTone(void) {
	//	int16_t audio;
	int32_t audio32;
	float sins = 0;
	BaseType_t res;
	int presc = 1000;
	int cntr = 1;

	while (1) {
		for (int n = 0; n < RINGBUFSIZE / 8; n++) {
			sins += 0.5 * 1000.0 * (2 * M_PI) / EXAMPLE_SAMPLE_RATE;
			audio32 = (int)(30000 * sin(sins));
			//	xRingbufferSend(pcmBuffer, &audio, 2, pdMS_TO_TICKS(1000));
			res = xRingbufferSend(pcmBuffer, &audio32, 4, pdMS_TO_TICKS(1000));
		}
		if (res == false) {
			ESP_LOGE(TAG, " fail sending ringbuffer");
		} else {
			presc--;
			if (presc == 0) {
				presc = 1000;
				ESP_LOGI(TAG, "send ok %d", cntr++);
			}
			// ESP_LOGE(TAG, "send ok");
		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		sins = 0;
	}
}
#ifndef S3BOARD
static void sendMic(void *args) {
	static int64_t last_packet_time_us;

	uint16_t *mic_data = malloc(EXAMPLE_RECV_BUF_SIZE + 10);
	uint16_t *mic_dataStereo = malloc(EXAMPLE_RECV_BUF_SIZE * 2 + 10);

	if (!mic_data) {
		ESP_LOGE(TAG, "[echo] No memory for read data buffer");
		abort();
	}
	if (!mic_dataStereo) {
		ESP_LOGE(TAG, "[echo] No memory for mic_dataStereo");
		abort();
	}

	esp_err_t ret = ESP_OK;
	size_t bytes_read = 0;
	size_t bytes_write = 0;
	ESP_LOGI(TAG, "[echo] Echo start");
	int presc = 1000;
	int cntr = 1;
	BaseType_t res;
	int packet_count = 0;

	while (1) {
		memset(mic_data, 0, EXAMPLE_RECV_BUF_SIZE);
		ret = esp_codec_dev_read(s_record_dev, mic_data, EXAMPLE_RECV_BUF_SIZE);
		bytes_read = (ret == ESP_OK) ? EXAMPLE_RECV_BUF_SIZE : 0;
		micBytes += bytes_read;

		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[echo] i2s read failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
			abort();
		}
		packet_count++;
		// if (packet_count % 100 == 0) {
		// 	int16_t *debug_samples = (int16_t *)mic_data;
		// 	int zero_count = 0;
		// 	int16_t max_val = 0;
		// 	int16_t min_val = 0;

		// 	// Check for data patterns
		// 	for (int i = 0; i < EXAMPLE_RECV_BUF_SIZE / 2; i++) {
		// 		if (debug_samples[i] == 0)
		// 			zero_count++;
		// 		if (debug_samples[i] > max_val)
		// 			max_val = debug_samples[i];
		// 		if (debug_samples[i] < min_val)
		// 			min_val = debug_samples[i];
		// 	}

		// 	//Check timing
		// 	int64_t now_us = esp_timer_get_time();
		// 	int64_t delta_us = last_packet_time_us ? (now_us - last_packet_time_us) : 0;
		// 	last_packet_time_us = now_us;

		// 	// Also check first few samples for continuity
		// 	ESP_LOGI(TAG, "Audio stats: zeros=%d/%d, range=[%d,%d], packets=%d, interval=%lldms", zero_count, EXAMPLE_RECV_BUF_SIZE / 2, min_val,
		// max_val, 			 packet_count, delta_us / 1000); 	ESP_LOGI(TAG, "First samples: [%d, %d, %d, %d, %d, %d, %d, %d ,]",
		// debug_samples[0], debug_samples[1], debug_samples[2], debug_samples[3], 			 debug_samples[4], debug_samples[5], debug_samples[6],
		// debug_samples[7]);

		// ret = esp_codec_dev_write(s_play_dev, mic_data, EXAMPLE_RECV_BUF_SIZE);
		// if (ret != ESP_OK) {
		// 	ESP_LOGE(TAG, "[echo] i2s write failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
		// 	abort();
		// }

		// for ( int m = 0; m < (bytes_read/100) ; m++) {
		// 	//xRingbufferSend(pcmBuffer, p, 2, pdMS_TO_TICKS(1000));
		// 	//res = xRingbufferSend(pcmBuffer, p++, 2, pdMS_TO_TICKS(1000));	// fill buffer
		// 	printf( "%d ",*p++);
		// }
		// printf( "\n\r");

		// uint16_t * p = mic_data;
		// uint16_t * p2 = mic_dataStereo;
		// for ( int m = 0; m < bytes_read/2 ; m++) {
		// 	*(p2) = *p;
		// 	*(p2+1) = *p;
		// 	p++;
		// 	p2 += 2;
		// }

		// res = xRingbufferSend(pcmBuffer, mic_dataStereo, bytes_read * 2, pdMS_TO_TICKS(1000));
		res = xRingbufferSend(pcmBuffer, mic_data, bytes_read, pdMS_TO_TICKS(1000));
		if (res == false) {
			ESP_LOGE(TAG, " fail sending ringbuffer");
		}

		else {
			presc--;
			if (presc == 0) {
				presc = 1000;
				ESP_LOGI(TAG, "send ok %d", cntr++);
			}
			// ESP_LOGE(TAG, "send ok");
		}
	}
	// ret = esp_codec_dev_write(s_play_dev, mic_data, EXAMPLE_RECV_BUF_SIZE);
}

#endif


static void sendMusic(void *args) {
	esp_err_t ret = ESP_OK;
	size_t bytes_write = 0;
	uint8_t *dataPtr = (uint8_t *)music_pcm_start;
	const size_t music_size = music_pcm_end - music_pcm_start - 1;
	int blockSize;
	int bytesToSend = music_size;

	while (1) {
		dataPtr = (uint8_t *)music_pcm_start;
		blockSize = 256;
		bytesToSend = music_size;
		micBytes = 0;
		ESP_LOGI(TAG, "Sending %d bytes", bytesToSend);

		// BaseType_t res = xRingbufferSend(pcmBuffer, dataPtr, music_size, pdMS_TO_TICKS(10000));
		// if (res == false)
		// 	ESP_LOGE(TAG, " fail sending ringbuffer");

		while (bytesToSend > 0) {
			if (blockSize > bytesToSend) {
				blockSize = bytesToSend;
			}

			BaseType_t res = xRingbufferSend(pcmBuffer, dataPtr, blockSize, pdMS_TO_TICKS(1000));
			if (res == false) {
				ESP_LOGE(TAG, " fail sending ringbuffer");
				bytesToSend = 0;
			} else {
				micBytes += blockSize;
				bytesToSend -= blockSize;
				dataPtr += blockSize;
			}
		}
		ESP_LOGE(TAG, "muxic send");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	vTaskDelete(NULL);
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

void app_main(void) {
	printf("start\n-----------------------------\n");
#ifdef S3BOARD
	initWifi();
#else
	initETH();

	if (es8311_codec_init() != ESP_OK) {
		ESP_LOGE(TAG, "es8311 codec init failed");
		abort();
	} else {
		ESP_LOGI(TAG, "es8311 codec init success");
	}

#endif
	pcmBuffer = xRingbufferCreate(RINGBUFSIZE, RINGBUF_TYPE_BYTEBUF);
	if (pcmBuffer == NULL) {
		printf("Failed to create ring buffer\n");
	} else {
		rtp_sender_init();
		rtp_sender_start();
		do {
			vTaskDelay(100);
		} while (!gotIP);

		xTaskCreate(timerTask, "timer", 4000, NULL, 5, NULL);
		xTaskCreate(sendMusic, "sendMusic", 8192, NULL, 5, NULL);
	//	generateTestTone();
		//	sendMic(NULL);
	}
}
