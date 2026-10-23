#ifndef BATCHTHERM_SDCARD_H
#define BATCHTHERM_SDCARD_H
#include "devices.h"
typedef struct {
  void *ctx;
  uint8_t (*transfer)(void *, uint8_t);
  void (*select)(void *, bool);
  uint32_t (*now_ms)(void *);
} sd_bus;
typedef struct {
  sd_bus bus;
  bool ready;
  uint32_t ocr;
} sd_card;
bt_result sd_initialize(sd_card *card);
bt_result sd_read_sector(sd_card *card, uint32_t sector, uint8_t data[512]);
bt_result sd_write_sector(sd_card *card, uint32_t sector,
                          const uint8_t data[512]);
#endif
