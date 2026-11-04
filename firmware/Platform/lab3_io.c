#include "lab3_io.h"
#include "main.h"
#include "timer_extend.h"
#include <stdarg.h>

extern UART_HandleTypeDef huart2;
static timer_extend clock_state;
static const uint32_t paint_word=0x3b2e177du;
extern uint32_t _estack, _Min_Stack_Size;

void perf_paint(void) {
  uint32_t *p=(uint32_t *)((uintptr_t)&_estack-(uintptr_t)&_Min_Stack_Size);
  uint32_t *safe=(uint32_t *)(__get_MSP()-64u);
  while (p<safe) *p++=paint_word;
}
unsigned perf_stack(void) {
  uint32_t *p=(uint32_t *)((uintptr_t)&_estack-(uintptr_t)&_Min_Stack_Size);
  while (p<&_estack && *p==paint_word) ++p;
  return (unsigned)((uintptr_t)&_estack-(uintptr_t)p);
}
void perf_start(void) {
  RCC->APBENR1 |= RCC_APBENR1_TIM3EN;
  TIM3->PSC=SystemCoreClock/1000000u-1u;
  TIM3->ARR=65535;
  TIM3->EGR=TIM_EGR_UG;
  TIM3->SR=0;
  TIM3->DIER=0;
  TIM3->CR1=TIM_CR1_CEN;
  clock_state=(timer_extend){.tick=HAL_GetTick(),.count=(uint16_t)TIM3->CNT};
}
uint32_t perf_us(void) {
  uint32_t primask=__get_PRIMASK();
  __disable_irq();
  uint32_t tick=HAL_GetTick();
  uint32_t elapsed=timer_advance(&clock_state,tick,(uint16_t)TIM3->CNT);
  __set_PRIMASK(primask);
  return elapsed;
}
uint32_t perf_wraps(void) {
  uint32_t primask=__get_PRIMASK();
  __disable_irq();
  uint32_t wraps=clock_state.wraps;
  __set_PRIMASK(primask);
  return wraps;
}

static char *number(char *p, const char *limit, uint32_t x, unsigned base) {
  char rev[10];
  unsigned n=0;
  do { rev[n++]="0123456789abcdef"[x%base]; x/=base; } while (x);
  while (n && p<limit) *p++=rev[--n];
  return p;
}
void bt_log(const char *format, ...) {
  char out[192], *p=out;
  const char *limit=out+sizeof(out)-2;
  va_list args;
  va_start(args,format);
  while (*format && p<limit) {
    char c=*format++;
    if (c!='%') { *p++=c; continue; }
    c=*format++;
    if (c=='s') {
      const char *s=va_arg(args,const char *);
      while (*s && p<limit) *p++=*s++;
    } else if (c=='d') {
      int32_t x=va_arg(args,int);
      if (x<0 && p<limit) *p++='-';
      p=number(p,limit,x<0?0u-(uint32_t)x:(uint32_t)x,10);
    } else if (c=='u' || c=='x') {
      p=number(p,limit,va_arg(args,unsigned),c=='u'?10:16);
    } else if (!c) break;
  }
  va_end(args);
  *p++='\r'; *p++='\n';
  (void)HAL_UART_Transmit(&huart2,(uint8_t *)out,(uint16_t)(p-out),50);
}
