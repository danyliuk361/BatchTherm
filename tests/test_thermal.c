#include "capture_vectors.h"
#include "devices.h"
#include "quality.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t calibration[22] = {
    0x01, 0x98, 0xff, 0xb8, 0xc7, 0xd1, 0x7f, 0xe5, 0x7f, 0xf5, 0x5a,
    0x71, 0x18, 0x2e, 0,    4,    0x80, 0,    0xdd, 0xf9, 0x0b, 0x34};
static bt_result fail_read, fail_write;
static int reads, writes, waits;
static uint8_t ident = 0x55;
static uint16_t test_ut = 27898;
static bt_result rd(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf,
                    size_t n) {
  (void)ctx;
  assert(addr == 0x77);
  ++reads;
  if (fail_read)
    return fail_read;
  if (reg == 0xd0) {
    assert(n == 1);
    buf[0] = ident;
  } else if (reg == 0xaa) {
    assert(n == 22);
    memcpy(buf, calibration, 22);
  } else {
    assert(reg == 0xf6 && n == 2);
    buf[0] = (uint8_t)(test_ut >> 8);
    buf[1] = (uint8_t)test_ut;
  }
  return BT_OK;
}
static bt_result wr(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf,
                    size_t n) {
  (void)ctx;
  assert(addr == 0x77 && reg == 0xf4 && n == 1 && buf[0] == 0x2e);
  ++writes;
  return fail_write;
}
static void delay(void *ctx, uint32_t ms) {
  (void)ctx;
  assert(ms >= 5);
  ++waits;
}
static void test_bmp(void) {
  bmp180 d = {.bus = {NULL, rd, wr, delay}, .address = 0x77};
  int16_t t = 0;
  uint16_t ut;
  assert(bmp180_measure(&d, &t, &ut) == BT_CAL);
  assert(bmp180_open(&d) == BT_OK && d.id == 0x55);
  assert(d.ac5 == 32757 && d.ac6 == 23153 && d.mc == -8711 && d.md == 2868);
  assert(bmp180_measure(&d, &t, &ut) == BT_OK && t == 150 && ut == 27898);
  assert(writes == 1 && waits == 1);
  for (bt_result e = BT_NACK; e <= BT_IO; e++) {
    fail_read = e;
    assert(bmp180_open(&d) == e && !d.ready);
    fail_read = BT_OK;
    assert(bmp180_open(&d) == BT_OK);
    fail_write = e;
    assert(bmp180_measure(&d, &t, &ut) == e);
    fail_write = BT_OK;
    fail_read = e;
    assert(bmp180_measure(&d, &t, &ut) == e);
    fail_read = BT_OK;
  }
  ident = 0x42;
  assert(bmp180_open(&d) == BT_ID);
  ident = 0x55;
  uint8_t old = calibration[8];
  calibration[8] = 0;
  calibration[9] = 0;
  assert(bmp180_open(&d) == BT_CAL);
  calibration[8] = old;
  calibration[9] = 0xf5;
  assert(bmp180_open(&d) == BT_OK);
  d.md = 0;
  assert(bmp180_compensate(&d, d.ac6, &t) == BT_CAL);
  d.md = 2868;
  assert(bmp180_compensate(&d, 65535, &t) == BT_RANGE);
  puts("PASS BMP180 datasheet 15.0 C, ID/calibration, conversion sequence, "
       "NACK/timeout/IO");
}
static uint16_t ntc_code(double c) {
  return (uint16_t)lround(4095.0 /
                          (1 + exp(-3977 * (1 / (c + 273.15) - 1 / 298.15))));
}
static void test_ntc(void) {
  int16_t t;
  assert(ntc_temperature(2048, &t) == BT_OK && abs(t - 250) <= 1);
  for (int c = 5; c <= 80; c += 5) {
    assert(ntc_temperature(ntc_code(c), &t) == BT_OK);
    assert(abs(t - c * 10) <= 1);
  }
  assert(ntc_temperature(0, &t) == BT_RANGE);
  assert(ntc_temperature(8, &t) == BT_RANGE);
  assert(ntc_temperature(4087, &t) == BT_RANGE);
  assert(ntc_temperature(4095, &t) == BT_RANGE);
  assert(ntc_temperature(100, &t) == BT_RANGE);
  assert(ntc_temperature(4000, &t) == BT_RANGE);
  puts("PASS NTC physical units, range, rail codes and 5..80 C independent "
       "model");
}
static void test_quality(void) {
  sample_filter f = {0};
  lid_filter l = {0};
  assert(!thermal_usable(&f, &l, BT_OK, 0));
  lid_observe(&l, true, 0);
  assert(!l.known);
  lid_observe(&l, false, 10);
  lid_observe(&l, true, 15);
  lid_observe(&l, true, 39);
  assert(!l.known);
  lid_observe(&l, true, 40);
  assert(l.known && l.closed);
  sample_observe(&f, BT_OK, 600);
  sample_observe(&f, BT_OK, 850);
  assert(!f.valid);
  sample_observe(&f, BT_OK, 600);
  assert(f.valid && f.filtered == 600);
  assert(thermal_in_band(&f, 600, 20));
  assert(thermal_usable(&f, &l, BT_OK, 600));
  assert(!thermal_usable(&f, &l, BT_OK, 601));
  assert(!thermal_usable(&f, &l, BT_TIMEOUT, 0));
  sample_observe(&f, BT_NACK, 0);
  assert(!f.valid && f.count == 0);
  sample_observe(&f, BT_OK, 580);
  sample_observe(&f, BT_OK, 580);
  sample_observe(&f, BT_OK, 580);
  assert(thermal_in_band(&f, 600, 20));
  sample_observe(&f, BT_OK, 620);
  sample_observe(&f, BT_OK, 620);
  assert(thermal_in_band(&f, 600, 20));
  sample_observe(&f, BT_OK, 621);
  sample_observe(&f, BT_OK, 621);
  assert(!thermal_in_band(&f, 600, 20));
  lid_observe(&l, false, 100);
  lid_observe(&l, false, 125);
  assert(!l.closed && !thermal_usable(&f, &l, BT_OK, 0));
  l = (lid_filter){0};
  lid_observe(&l, true, 0xfffffff0u);
  lid_observe(&l, true, 9);
  assert(l.known && l.closed);
  puts("PASS median warm-up/spike/reset, inclusive band, freshness, lid "
       "bounce/wrap");
}
static void test_capture(void) {
  memcpy(calibration, capture_calibration, 22);
  ident = 0x55;
  fail_read = fail_write = BT_OK;
  test_ut = CAPTURE_UT;
  bmp180 d = {.bus = {NULL, rd, wr, delay}, .address = 0x77};
  int16_t t;
  uint16_t raw;
  assert(bmp180_open(&d) == BT_OK);
  assert(bmp180_measure(&d, &t, &raw) == BT_OK && t == CAPTURE_AMBIENT_DC &&
         raw == CAPTURE_UT);
  assert(ntc_temperature(CAPTURE_ADC, &t) == BT_OK && t == CAPTURE_SAMPLE_DC);
  puts("PASS own VCD calibration/UT and UART/ADC vectors: ambient=24.0 C "
       "sample=60.0 C");
}
void test_storage(void);
int main(void) {
  test_storage();
  test_bmp();
  test_ntc();
  test_quality();
  test_capture();
  puts("ALL THERMAL TESTS PASS");
  return 0;
}
