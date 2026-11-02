#include "batch.h"
#include <string.h>

bool profile_valid(thermal_profile p) {
  return p.target >= 400 && p.target <= 750 && p.band >= 10 && p.band <= 50 &&
         p.hold_s >= 5 && p.hold_s <= 120 && p.cool >= 200 && p.cool <= 400 &&
         p.cool < p.target - p.band;
}

bool batch_active(const batch *b) {
  return b->state == RAMP || b->state == SOAK || b->state == COOL;
}

static void enter(batch *b, phase state, reason why, uint32_t now) {
  b->state = state;
  b->why = why;
  b->entered = now;
}

void batch_begin(batch *b, uint32_t id, thermal_profile p, uint32_t now, int16_t sample) {
  *b = (batch){.id=id,.entered=now,.profile=p,.minimum=sample,.maximum=sample,.state=RAMP};
}

bool batch_interrupt(batch *b, reason why, uint32_t now) {
  if (!batch_active(b))
    return false;
  enter(b, INTERRUPTED, why, now);
  return true;
}

bool batch_deadline(batch *b, uint32_t now) {
  if ((uint32_t)(now - b->entered) < 180000u)
    return false;
  if (b->state == RAMP)
    return batch_interrupt(b, RAMP_TIMEOUT, now);
  if (b->state == COOL)
    return batch_interrupt(b, COOL_TIMEOUT, now);
  return false;
}

bool batch_frame(batch *b, uint32_t now, int16_t sample) {
  if (!batch_active(b))
    return false;
  if (batch_deadline(b, now))
    return true;
  uint32_t gap = now - b->previous;
  b->previous = now;
  if (sample < b->minimum) b->minimum = sample;
  if (sample > b->maximum) b->maximum = sample;
  bool in_band = sample >= b->profile.target - b->profile.band &&
                 sample <= b->profile.target + b->profile.band;
  switch (b->state) {
  case RAMP:
    if (!in_band) return false;
    b->held = 0;
    b->have_frame = true;
    enter(b, SOAK, NORMAL, now);
    return true;
  case SOAK:
    if (!in_band || !b->have_frame || gap > 250) {
      b->held = 0;
      enter(b, RAMP, in_band ? FRAME_GAP : OUT_OF_BAND, now);
      return true;
    }
    b->held += gap;
    if (b->held < b->profile.hold_s * 1000u) return false;
    enter(b, COOL, NORMAL, now);
    return true;
  case COOL:
    if (sample > b->profile.cool) return false;
    enter(b, DONE, NORMAL, now);
    return true;
  default:
    return false;
  }
}

/* Append-only, one physical sector per logical 64-byte record. The seal at the
 * end of the sector prevents a prefix-only torn write from looking complete. */
static uint32_t crc32(const uint8_t *p, unsigned count) {
  uint32_t crc = UINT32_MAX;
  while (count--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
static void put(uint8_t *p, uint32_t value, unsigned bytes) {
  while (bytes--) { *p++ = (uint8_t)value; value >>= 8; }
}
static uint32_t get(const uint8_t *p, unsigned bytes) {
  uint32_t result = 0;
  while (bytes) result = (result << 8) | p[--bytes];
  return result;
}
void journal_encode(uint8_t sector[512], const journal_record *r) {
  memset(sector, 0, 512);
  put(sector, 0x31544842u, 4);
  put(sector+4,r->sequence,4);
  put(sector+8,r->id,4);
  put(sector+12,r->time,4);
  put(sector+16,r->held,4);
  put(sector+20,(uint16_t)r->profile.target,2);
  put(sector+22,(uint16_t)r->profile.band,2);
  put(sector+24,(uint16_t)r->profile.cool,2);
  put(sector+26,r->profile.hold_s,2);
  put(sector+28,(uint16_t)r->sample,2);
  put(sector+30,(uint16_t)r->ambient,2);
  put(sector+32,(uint16_t)r->minimum,2);
  put(sector+34,(uint16_t)r->maximum,2);
  sector[36] = (uint8_t)r->state;
  sector[37] = (uint8_t)r->why;
  uint32_t crc = crc32(sector,60);
  put(sector+60,crc,4);
  /* Each seal byte is nonzero: any zero-filled suffix is detectable. */
  for (unsigned i=0;i<4;++i) sector[508+i]=(uint8_t)(1u+((crc>>(i*8))%255u));
}
bool journal_decode(journal_record *r, const uint8_t sector[512], uint32_t sequence) {
  if (get(sector,4)!=0x31544842u || get(sector+4,4)!=sequence ||
      sector[36]<RAMP || sector[36]>INTERRUPTED || sector[37]>OUT_OF_BAND)
    return false;
  uint32_t crc=crc32(sector,60);
  if (get(sector+60,4)!=crc) return false;
  for (unsigned i=0;i<4;++i)
    if (sector[508+i]!=(uint8_t)(1u+((crc>>(i*8))%255u))) return false;
  thermal_profile p={(int16_t)get(sector+20,2),(int16_t)get(sector+22,2),
                     (int16_t)get(sector+24,2),(uint16_t)get(sector+26,2)};
  if (!profile_valid(p)) return false;
  *r=(journal_record){sequence,get(sector+8,4),get(sector+12,4),get(sector+16,4),p,
    (int16_t)get(sector+28,2),(int16_t)get(sector+30,2),(int16_t)get(sector+32,2),
    (int16_t)get(sector+34,2),(phase)sector[36],(reason)sector[37],0};
  return true;
}
