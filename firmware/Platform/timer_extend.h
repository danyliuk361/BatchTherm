#ifndef BATCHTHERM_TIMER_EXTEND_H
#define BATCHTHERM_TIMER_EXTEND_H
#include <stdint.h>

typedef struct {
  uint32_t tick, us, wraps;
  uint16_t count;
} timer_extend;

/* The millisecond clock only resolves whole 65536-us turns. The elapsed
 * microseconds come from CNT. Samples must be close enough that the clocks'
 * relative drift plus tick quantization stays below half a turn. */
static inline uint32_t timer_advance(timer_extend *s, uint32_t tick, uint16_t count) {
  uint32_t modulo=(uint16_t)(count-s->count);
  int32_t delta=(int32_t)modulo-(modulo>=32768u?65536:0);
  int32_t coarse=(int32_t)((tick-s->tick)*1000u);
  delta+=((coarse-delta+32768)/65536)*65536;
  if (delta<0) return s->us;
  s->wraps+=((uint32_t)s->count+(uint32_t)delta)>>16;
  s->us+=(uint32_t)delta;
  s->tick=tick;
  s->count=count;
  return s->us;
}
#endif
