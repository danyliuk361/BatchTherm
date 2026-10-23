#include "quality.h"
void lid_observe(lid_filter *l, bool raw_closed, uint32_t now) {
  if (!l->initialized || l->candidate != raw_closed) {
    l->candidate = raw_closed;
    l->changed = now;
    l->initialized = true;
  }
  if ((uint32_t)(now - l->changed) >= 25) {
    l->closed = l->candidate;
    l->known = true;
  }
}
void sample_observe(sample_filter *s, bt_result result, int16_t value) {
  if (result != BT_OK) {
    s->valid = false;
    s->count = 0;
    s->next = 0;
    return;
  }
  s->values[s->next] = value;
  s->next = (uint8_t)((s->next + 1) % 3);
  if (s->count < 3)
    ++s->count;
  s->valid = s->count == 3;
  if (s->valid) {
    int16_t a = s->values[0], b = s->values[1], c = s->values[2], t;
    if (a > b) {
      t = a;
      a = b;
      b = t;
    }
    if (b > c) {
      t = b;
      b = c;
      c = t;
    }
    if (a > b) {
      t = a;
      a = b;
      b = t;
    }
    s->filtered = b;
  }
}
bool thermal_usable(const sample_filter *s, const lid_filter *l,
                    bt_result ambient_status, uint32_t age_ms) {
  return s->valid && l->known && l->closed && ambient_status == BT_OK &&
         age_ms <= 600;
}
bool thermal_in_band(const sample_filter *s, int16_t target, int16_t band) {
  return s->valid && s->filtered >= target - band &&
         s->filtered <= target + band;
}
