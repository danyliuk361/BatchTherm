#include "timer_extend.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  timer_extend clock={.tick=50,.count=65534};
  assert(timer_advance(&clock,50,65535)==1);
  assert(timer_advance(&clock,50,0)==2);
  assert(clock.wraps==1);
  assert(timer_advance(&clock,50,65535)==2); /* stale sample at boundary */
  assert(timer_advance(&clock,50,3)==5);
  assert(timer_advance(&clock,90,40003)==40005);
  assert(timer_advance(&clock,290,(uint16_t)(40003+200000))==240005);
  assert(clock.wraps==4);
  clock=(timer_extend){.tick=UINT32_MAX,.us=UINT32_MAX-5,.count=65530};
  assert(timer_advance(&clock,0,6)==6);
  assert(clock.wraps==1);
  puts("TIM3 extension: rollover, stale sample, multiwrap and uint32 passed");
}
