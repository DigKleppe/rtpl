
void udpServerTask (void *pvParameters);

#include "freertos/ringbuf.h"

typedef struct {
    int port;
    RingbufHandle_t ringBuf;
} udpTaskParams_t;

#include "build_config.h"

#define UDPBUFSIZE CONFIG_PCM_CHUNK_SIZE + 12   // 12 = rtp header#include "sdkconfig.h"


extern QueueHandle_t udpMssgBox;