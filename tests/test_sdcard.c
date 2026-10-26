#include "sdcard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  bool selected;
  uint8_t command[6], reply[520], data[512];
  unsigned cmdlen, rpos, rlen, writepos, writephase, app_tries;
  uint8_t history[32];
  unsigned history_count;
  uint32_t now;
  int fault;
} card_fake;
static void reply(card_fake *f, uint8_t r) {
  f->rpos = 0;
  f->rlen = 1;
  f->reply[0] = r;
}
static uint8_t exchange(void *ctx, uint8_t v) {
  card_fake *f = ctx;
  f->now++;
  if (!f->selected) {
    assert(v == 0xff);
    return 0xff;
  }
  if (f->rpos < f->rlen)
    return f->reply[f->rpos++];
  if (f->fault == 5 && f->writephase == 4)
    return 0;
  if (f->writephase == 1) {
    if (v == 0xfe) {
      f->writephase = 2;
      f->writepos = 0;
    }
    return 0xff;
  }
  if (f->writephase == 2) {
    f->data[f->writepos++] = v;
    if (f->writepos == 512) {
      f->writephase = 3;
      f->writepos = 0;
    }
    return 0xff;
  }
  if (f->writephase == 3) {
    if (++f->writepos == 2) {
      f->writephase = 4;
      reply(f, f->fault == 4 ? 0x0b : 5);
    }
    return 0xff;
  }
  if (f->cmdlen || ((v & 0xc0) == 0x40)) {
    f->command[f->cmdlen++] = v;
    if (f->cmdlen == 6) {
      unsigned cmd = f->command[0] & 0x3f;
      f->history[f->history_count++] = (uint8_t)cmd;
      f->cmdlen = 0;
      if (cmd == 0) {
        assert(f->command[5] == 0x95);
        reply(f, f->fault == 1 ? 0xff : 1);
      } else if (cmd == 8) {
        assert(f->command[3] == 1 && f->command[4] == 0xaa &&
               f->command[5] == 0x87);
        reply(f, 1);
        f->reply[f->rlen++] = 0;
        f->reply[f->rlen++] = 0;
        f->reply[f->rlen++] = 1;
        f->reply[f->rlen++] = f->fault == 2 ? 0 : 0xaa;
      } else if (cmd == 55)
        reply(f, 1);
      else if (cmd == 41)
        reply(f, f->app_tries++ == 0 ? 1 : 0);
      else if (cmd == 58) {
        reply(f, 0);
        f->reply[f->rlen++] = 0xc0;
        f->reply[f->rlen++] = 0xff;
        f->reply[f->rlen++] = 0x80;
        f->reply[f->rlen++] = 0;
      } else if (cmd == 17) {
        reply(f, 0);
        f->reply[f->rlen++] = f->fault == 3 ? 0x0b : 0xfe;
        memcpy(f->reply + f->rlen, f->data, 512);
        f->rlen += 512;
        f->reply[f->rlen++] = 0xff;
        f->reply[f->rlen++] = 0xff;
      } else {
        assert(cmd == 24 && f->command[4] == 32);
        reply(f, 0);
        f->writephase = 1;
      }
    }
    return 0xff;
  }
  return 0xff;
}
static void selected(void *ctx, bool active) {
  card_fake *f = ctx;
  f->selected = active;
  if (!active) {
    f->cmdlen = 0;
    f->rpos = f->rlen = 0;
    f->writephase = 0;
  }
}
static uint32_t clock_ms(void *ctx) {
  card_fake *f = ctx;
  return f->now;
}
void test_storage(void) {
  card_fake f = {0};
  sd_card c = {.bus = {&f, exchange, selected, clock_ms}};
  uint8_t sent[512], got[512];
  assert(sd_read_sector(&c, 32, got) == BT_IO);
  assert(sd_write_sector(&c, 32, got) == BT_IO);
  assert(sd_initialize(&c) == BT_OK && c.ready && c.ocr == 0xc0ff8000);
  const uint8_t seq[] = {0, 8, 55, 41, 55, 41, 58};
  assert(f.history_count == sizeof(seq) &&
         !memcmp(seq, f.history, sizeof(seq)));
  for (unsigned n = 0; n < 512; ++n)
    sent[n] = (uint8_t)(n * 37 + 19);
  assert(sd_write_sector(&c, 32, sent) == BT_OK);
  assert(sd_read_sector(&c, 32, got) == BT_OK && !memcmp(sent, got, 512));
  f.fault = 3;
  assert(sd_read_sector(&c, 32, got) == BT_IO);
  f.fault = 4;
  assert(sd_write_sector(&c, 32, sent) == BT_IO);
  f.fault = 5;
  assert(sd_write_sector(&c, 32, sent) == BT_TIMEOUT);
  for (int fault = 1; fault <= 2; ++fault) {
    f = (card_fake){.fault = fault};
    assert(sd_initialize(&c) == (fault == 1 ? BT_IO : BT_ID));
    assert(!c.ready);
  }
  puts("PASS SD command order/CRC, SDHC OCR, CMD24/CMD17 512-byte readback, "
       "bad echo/token/response and busy timeout");
}
