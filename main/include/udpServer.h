
void udpServerTask (void *pvParameters);

#include "freertos/ringbuf.h"

typedef struct {
    int port;
    RingbufHandle_t ringBuf;
} udpTaskParams_t;


#define UDPBUFSIZE 1024


extern QueueHandle_t udpMssgBox;