/*
 * udpServer.cpp
 *
 *  Created on: May 2, 2022
 *      Author: dig
 */

// Server side implementation of UDP client-server model
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "udpServer.h"

#define TAG "UDPserver"

void udpServerTask(void *pvParameters) {
	udpTaskParams_t udpTaskParams = *(udpTaskParams_t *)pvParameters;
	int sockfd;
	bool err = false;
	struct sockaddr_in servaddr, cliaddr;
	int len, n;

	char *buffer = (char *)malloc((size_t)UDPBUFSIZE);
	if (buffer == NULL) {
		ESP_LOGE(TAG, "buffer failed");
		vTaskDelete(NULL);
	}

	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));

	// Filling server information
	servaddr.sin_family = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(udpTaskParams.port);
	len = sizeof(cliaddr); 

	while (1) {
		if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			ESP_LOGE(TAG, "socket creation failed");
			err = true;
		} else {
			// Bind the socket with the server address
			if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
				ESP_LOGE(TAG, "bind failed");
				err = true;
				closesocket(sockfd);
			} else {
				//		ESP_LOGI(TAG, "server running");
				n = recvfrom(sockfd, buffer, UDPBUFSIZE, MSG_WAITALL, (struct sockaddr *)&cliaddr, (socklen_t *)&len);
				UBaseType_t res = xRingbufferSend(udpTaskParams.ringBuf, buffer, n, 0);
				if (res != pdTRUE) {
					ESP_LOGE(TAG, "error writing ringbuffer");
				} 
				ESP_LOGI(TAG, "received %d bytes", n);
			}
			closesocket(sockfd);
		}
	}
	if (err) {
		vTaskDelay(100);
		err = false;
	}
}

