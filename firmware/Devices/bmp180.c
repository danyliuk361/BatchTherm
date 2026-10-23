#include "devices.h"
static uint16_t be16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] * 256u + p[1]);
}
bt_result bmp180_open(bmp180 *d) {
  uint8_t c[22];
  d->ready = false;
  bt_result r = d->bus.read(d->bus.ctx, d->address, 0xd0, &d->id, 1);
  if (r != BT_OK)
    return r;
  if (d->id != 0x55)
    return BT_ID;
  r = d->bus.read(d->bus.ctx, d->address, 0xaa, c, sizeof(c));
  if (r != BT_OK)
    return r;
  d->ac5 = be16(c + 8);
  d->ac6 = be16(c + 10);
  d->mc = (int16_t)be16(c + 18);
  d->md = (int16_t)be16(c + 20);
  if (!d->ac5 || d->ac5 == 0xffff || !d->ac6 || d->ac6 == 0xffff)
    return BT_CAL;
  d->ready = true;
  return BT_OK;
}
bt_result bmp180_compensate(const bmp180 *d, uint16_t ut, int16_t *decic) {
  /* Bosch temperature algorithm; 64-bit intermediates avoid corrupt-calibration
   * overflow. */
  int64_t x1 = ((int64_t)ut - d->ac6) * d->ac5 / 32768;
  int64_t divisor = x1 + d->md;
  if (!divisor)
    return BT_CAL;
  int64_t x2 = (int64_t)d->mc * 2048 / divisor;
  int64_t t = (x1 + x2 + 8) / 16;
  if (t < 0 || t > 500)
    return BT_RANGE;
  *decic = (int16_t)t;
  return BT_OK;
}
bt_result bmp180_measure(bmp180 *d, int16_t *decic, uint16_t *raw) {
  if (!d->ready)
    return BT_CAL;
  uint8_t command = 0x2e, bytes[2];
  bt_result r = d->bus.write(d->bus.ctx, d->address, 0xf4, &command, 1);
  if (r != BT_OK)
    return r;
  d->bus.wait_ms(d->bus.ctx, 5);
  r = d->bus.read(d->bus.ctx, d->address, 0xf6, bytes, 2);
  if (r != BT_OK)
    return r;
  *raw = be16(bytes);
  return bmp180_compensate(d, *raw, decic);
}
