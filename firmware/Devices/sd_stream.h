#ifndef BATCHTHERM_SD_STREAM_H
#define BATCHTHERM_SD_STREAM_H
#include "sdcard.h"
typedef struct {
  sd_card *card;
  uint8_t *data;
  uint32_t since;
  uint16_t position;
  uint8_t phase;
  bt_result result;
} sd_stream;
bool sd_stream_begin(sd_stream *s, sd_card *card, uint32_t sector, uint8_t data[512], bool write);
bool sd_stream_step(sd_stream *s);
#endif
