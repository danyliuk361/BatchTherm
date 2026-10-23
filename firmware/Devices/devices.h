#ifndef BATCHTHERM_DEVICES_H
#define BATCHTHERM_DEVICES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {
  BT_OK = 0,
  BT_NACK,
  BT_TIMEOUT,
  BT_IO,
  BT_ID,
  BT_CAL,
  BT_RANGE
} bt_result;
typedef struct {
  void *ctx;
  bt_result (*read)(void *, uint8_t, uint8_t, uint8_t *, size_t);
  bt_result (*write)(void *, uint8_t, uint8_t, const uint8_t *, size_t);
  void (*wait_ms)(void *, uint32_t);
} bt_bus;
typedef struct {
  bt_bus bus;
  uint8_t address, id;
  uint16_t ac5, ac6;
  int16_t mc, md;
  bool ready;
} bmp180;
bt_result bmp180_open(bmp180 *d);
bt_result bmp180_compensate(const bmp180 *d, uint16_t ut, int16_t *decic);
bt_result bmp180_measure(bmp180 *d, int16_t *decic, uint16_t *raw);
bt_result ntc_temperature(uint16_t adc, int16_t *decic);
#endif
