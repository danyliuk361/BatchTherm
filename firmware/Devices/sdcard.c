#include "sdcard.h"
static uint8_t tx(sd_card *c, uint8_t v) {
  return c->bus.transfer(c->bus.ctx, v);
}
static void end(sd_card *c) {
  c->bus.select(c->bus.ctx, false);
  (void)tx(c, 0xff);
}
static uint8_t command(sd_card *c, uint8_t cmd, uint32_t arg) {
  c->bus.select(c->bus.ctx, true);
  (void)tx(c, 0xff);
  (void)tx(c, (uint8_t)(0x40 | cmd));
  for (int n = 24; n >= 0; n -= 8)
    (void)tx(c, (uint8_t)(arg >> n));
  (void)tx(c, cmd == 0 ? 0x95 : cmd == 8 ? 0x87 : 1);
  for (unsigned n = 0; n < 10; ++n) {
    uint8_t r = tx(c, 0xff);
    if (!(r & 0x80))
      return r;
  }
  return 0xff;
}
bt_result sd_initialize(sd_card *c) {
  c->ready = false;
  c->bus.select(c->bus.ctx, false);
  for (unsigned n = 0; n < 10; ++n)
    (void)tx(c, 0xff);
  uint8_t r = command(c, 0, 0);
  end(c);
  if (r != 1)
    return BT_IO;
  r = command(c, 8, 0x1aa);
  uint32_t echo = 0;
  for (unsigned n = 0; n < 4; ++n)
    echo = (echo << 8) | tx(c, 0xff);
  end(c);
  if (r != 1 || echo != 0x1aa)
    return BT_ID;
  uint32_t started = c->bus.now_ms(c->bus.ctx);
  do {
    r = command(c, 55, 0);
    end(c);
    if (r > 1)
      return BT_IO;
    r = command(c, 41, 0x40000000);
    end(c);
    if ((uint32_t)(c->bus.now_ms(c->bus.ctx) - started) > 1000)
      return BT_TIMEOUT;
  } while (r == 1);
  if (r)
    return BT_IO;
  r = command(c, 58, 0);
  c->ocr = 0;
  for (unsigned n = 0; n < 4; ++n)
    c->ocr = (c->ocr << 8) | tx(c, 0xff);
  end(c);
  if (r || !(c->ocr & 0x40000000))
    return BT_ID; /* Only SDHC block-addressing is supported. */
  c->ready = true;
  return BT_OK;
}
bt_result sd_read_sector(sd_card *c, uint32_t sector, uint8_t data[512]) {
  if (!c->ready)
    return BT_IO;
  uint8_t r = command(c, 17, sector);
  if (r) {
    end(c);
    return BT_IO;
  }
  uint32_t started = c->bus.now_ms(c->bus.ctx);
  do {
    r = tx(c, 0xff);
    if ((uint32_t)(c->bus.now_ms(c->bus.ctx) - started) > 200) {
      end(c);
      return BT_TIMEOUT;
    }
  } while (r == 0xff);
  if (r != 0xfe) {
    end(c);
    return BT_IO;
  }
  for (unsigned n = 0; n < 512; ++n)
    data[n] = tx(c, 0xff);
  (void)tx(c, 0xff);
  (void)tx(c, 0xff);
  end(c);
  return BT_OK;
}
bt_result sd_write_sector(sd_card *c, uint32_t sector,
                          const uint8_t data[512]) {
  if (!c->ready)
    return BT_IO;
  uint8_t r = command(c, 24, sector);
  if (r) {
    end(c);
    return BT_IO;
  }
  (void)tx(c, 0xff);
  (void)tx(c, 0xfe);
  for (unsigned n = 0; n < 512; ++n)
    (void)tx(c, data[n]);
  (void)tx(c, 0xff);
  (void)tx(c, 0xff);
  r = tx(c, 0xff);
  if ((r & 0x1f) != 5) {
    end(c);
    return BT_IO;
  }
  uint32_t started = c->bus.now_ms(c->bus.ctx);
  while (tx(c, 0xff) != 0xff)
    if ((uint32_t)(c->bus.now_ms(c->bus.ctx) - started) > 500) {
      end(c);
      return BT_TIMEOUT;
    }
  end(c);
  return BT_OK;
}
