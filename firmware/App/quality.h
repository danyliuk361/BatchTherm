#ifndef BATCHTHERM_QUALITY_H
#define BATCHTHERM_QUALITY_H
#include "devices.h"
typedef struct {
  bool initialized, candidate, closed, known;
  uint32_t changed;
} lid_filter;
void lid_observe(lid_filter *l, bool raw_closed, uint32_t now);
typedef struct {
  int16_t values[3], filtered;
  uint8_t next, count;
  bool valid;
} sample_filter;
void sample_observe(sample_filter *s, bt_result result, int16_t value);
bool thermal_usable(const sample_filter *s, const lid_filter *l,
                    bt_result ambient_status, uint32_t age_ms);
bool thermal_in_band(const sample_filter *s, int16_t target, int16_t band);
#endif
