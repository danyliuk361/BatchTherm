#include "board.h"
#include "devices.h"
#include "main.h"
#include "quality.h"
#include "lab3_io.h"
#include <stdlib.h>
#include <string.h>
#ifndef BT_BUILD
#define BT_BUILD "development"
#endif

extern UART_HandleTypeDef huart2;
extern volatile uint32_t heap_attempts;
static ADC_HandleTypeDef adc;
static bmp180 ambient;
static sample_filter sample;
static lid_filter lid;
static batch current;
static thermal_profile profile={600,20,350,30};
static bt_result ambient_result=BT_CAL, ntc_result=BT_CAL;
static int16_t ambient_dc, sample_dc;
static uint16_t adc_raw, ut;
static uint32_t ambient_time, due_lid, due_sample, due_bmp, due_beat;
static uint32_t previous_loop, previous_sample, previous_bmp, previous_lid;
static uint32_t miss_lid, miss_sample, miss_bmp, busy_us, window_begin;
static uint32_t adc_count, bmp_count, error_count, commands, rejected, beats;
static uint32_t abort_lf, raw_seen_us;
static bool first_frame, raw_closed, raw_known;
static uint8_t received_byte;
static volatile uint8_t rx_ring[128], rx_head, rx_tail;
static volatile uint32_t lf_time[128], rx_overflow, rx_errors, rx_rearm;
static volatile bool rx_rearm_pending;
static uint32_t seen_loss;
static char line[80];
static unsigned used;
static bool discard;
enum metric_id { LOOP, LID_TASK, NTC_TASK, BMP_TASK, RX_TASK, SD_TASK, HEART_TASK,
                 ADC_GATE, LID_PERIOD, SAMPLE_PERIOD, BMP_PERIOD, LID_LAT, STATUS_LAT, ABORT_LAT, METRIC_COUNT };
typedef struct { uint32_t n, min, max, sum; } metric;
static metric metrics[METRIC_COUNT];

static void metric_add(enum metric_id id, uint32_t value) {
  metric *m=&metrics[id];
  if (!m->n || value<m->min) m->min=value;
  if (value>m->max) m->max=value;
  ++m->n; m->sum+=value;
}
static void busy(enum metric_id id,uint32_t begin) {
  uint32_t elapsed=perf_us()-begin;
  metric_add(id,elapsed);
  busy_us+=elapsed;
}
static bool release(uint32_t *due, uint32_t period, uint32_t *miss, uint32_t *prev,enum metric_id metric_id) {
  uint32_t now=HAL_GetTick();
  if ((int32_t)(now-*due)<0) return false;
  uint32_t skipped=(now-*due)/period;
  *miss+=skipped;
  *due+=(skipped+1)*period;
  uint32_t t=perf_us();
  if (*prev) metric_add(metric_id,t-*prev);
  *prev=t;
  return true;
}

static void receive_next(void) {
  HAL_StatusTypeDef result=HAL_UART_Receive_IT(&huart2,&received_byte,1);
  if (result==HAL_OK) rx_rearm_pending=false;
  else if (huart2.RxState==HAL_UART_STATE_READY) {
    ++rx_rearm;
    rx_rearm_pending=true;
  }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle) {
  if (handle!=&huart2) return;
  uint8_t head=rx_head;
  uint8_t next=(uint8_t)((head+1u)&127u);
  if (next==rx_tail) ++rx_overflow;
  else {
    rx_ring[head]=received_byte;
    if (received_byte=='\n') lf_time[head]=perf_us();
    rx_head=next;
  }
  receive_next();
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
  if (handle==&huart2) {
    ++rx_errors;
    receive_next();
  }
}
/* Software I2C over HAL open-drain GPIO. This avoids the observed Wokwi
 * hardware-I2C transfer-completion incompatibility; device logic is unchanged.
 */
static void half_bit(void) {
  for (volatile unsigned i = 0; i < 25; ++i)
    __NOP();
}
static void line_level(uint16_t number, int high) {
  GPIO_InitTypeDef pin = {0};
  pin.Pin = number;
  pin.Mode = high ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT_OD;
  pin.Pull = GPIO_PULLUP;
  pin.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_WritePin(GPIOB, number, GPIO_PIN_RESET);
  HAL_GPIO_Init(GPIOB, &pin);
  half_bit();
}
static void scl(int high) { line_level(GPIO_PIN_8, high); }
static void sda(int high) { line_level(GPIO_PIN_9, high); }
static bt_result clock_high(void) {
  scl(1);
  uint32_t start = HAL_GetTick();
  while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET)
    if ((uint32_t)(HAL_GetTick() - start) >= 10)
      return BT_TIMEOUT;
  return BT_OK;
}
static void stop_bus(void) {
  scl(0);
  sda(0);
  scl(1);
  sda(1);
}
static bt_result start_bus(void) {
  sda(1);
  bt_result r = clock_high();
  if (r != BT_OK)
    return r;
  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
    return BT_TIMEOUT;
  sda(0);
  scl(0);
  return BT_OK;
}
static bt_result send_byte(uint8_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    sda(value & 0x80);
    value <<= 1;
    if (clock_high() != BT_OK)
      return BT_TIMEOUT;
    scl(0);
  }
  sda(1);
  if (clock_high() != BT_OK)
    return BT_TIMEOUT;
  bool ack = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET;
  scl(0);
  return ack ? BT_OK : BT_NACK;
}
static bt_result get_byte(uint8_t *value, bool last) {
  *value = 0;
  sda(1);
  for (unsigned i = 0; i < 8; ++i) {
    if (clock_high() != BT_OK)
      return BT_TIMEOUT;
    *value = (uint8_t)((*value << 1) |
                       (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET));
    scl(0);
  }
  sda(last);
  if (clock_high() != BT_OK)
    return BT_TIMEOUT;
  scl(0);
  sda(1);
  return BT_OK;
}
static bt_result read_reg(void *ctx, uint8_t addr, uint8_t reg, uint8_t *data,
                          size_t n) {
  (void)ctx;
  bt_result r = start_bus();
  if (r == BT_OK)
    r = send_byte((uint8_t)(addr << 1));
  if (r == BT_OK)
    r = send_byte(reg);
  if (r == BT_OK)
    r = start_bus();
  if (r == BT_OK)
    r = send_byte((uint8_t)((addr << 1) | 1));
  for (size_t i = 0; i < n && r == BT_OK; ++i)
    r = get_byte(data + i, i + 1 == n);
  stop_bus();
  return r;
}
static bt_result write_reg(void *ctx, uint8_t addr, uint8_t reg,
                           const uint8_t *data, size_t n) {
  (void)ctx;
  bt_result r = start_bus();
  if (r == BT_OK)
    r = send_byte((uint8_t)(addr << 1));
  if (r == BT_OK)
    r = send_byte(reg);
  for (size_t i = 0; i < n && r == BT_OK; ++i)
    r = send_byte(data[i]);
  stop_bus();
  return r;
}
static void wait_ms(void *ctx, uint32_t ms) {
  (void)ctx;
  HAL_Delay(ms);
}
static void adc_init(void) {
  __HAL_RCC_ADC_CLK_ENABLE();
  HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  (void)HAL_UART_Receive_IT(&huart2, &received_byte, 1);
  GPIO_InitTypeDef pin = {0};
  pin.Pin = GPIO_PIN_0;
  pin.Mode = GPIO_MODE_ANALOG;
  pin.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &pin);
  adc.Instance = ADC1;
  adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  adc.Init.Resolution = ADC_RESOLUTION_12B;
  adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
  adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  adc.Init.NbrOfConversion = 1;
  adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  adc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_79CYCLES_5;
  adc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_79CYCLES_5;
  adc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&adc) != HAL_OK ||
      HAL_ADCEx_Calibration_Start(&adc) != HAL_OK)
    Error_Handler();
  ADC_ChannelConfTypeDef channel = {0};
  channel.Channel = ADC_CHANNEL_0;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&adc, &channel) != HAL_OK)
    Error_Handler();
}
static void sample_read(void) {
  uint32_t t = perf_us();
  GPIOA->BSRR=GPIO_PIN_8;
  HAL_StatusTypeDef r = HAL_ADC_Start(&adc);
  if (r == HAL_OK)
    r = HAL_ADC_PollForConversion(&adc, 5);
  if (r == HAL_OK) {
    adc_raw = (uint16_t)HAL_ADC_GetValue(&adc);
    ntc_result = ntc_temperature(adc_raw, &sample_dc);
  } else
    ntc_result = r == HAL_TIMEOUT ? BT_TIMEOUT : BT_IO;
  (void)HAL_ADC_Stop(&adc);
  GPIOA->BSRR=(uint32_t)GPIO_PIN_8<<16;
  metric_add(ADC_GATE,perf_us()-t);
  sample_observe(&sample, ntc_result, sample_dc);

}

static bool ambient_valid(uint32_t now) {
  return ambient_result==BT_OK && now-ambient_time<=600u;
}
static void signal_phase(void) {
  bool active=batch_active(&current);
  GPIOA->BSRR=active?GPIO_PIN_9:((uint32_t)GPIO_PIN_9<<16);
  GPIOA->BSRR=current.state==SOAK?GPIO_PIN_5:((uint32_t)GPIO_PIN_5<<16);
}
static void transition(void) {
  uint32_t now=HAL_GetTick();
  if (batch_active(&current) && storage_space()<=1)
    batch_interrupt(&current,JOURNAL_FULL,now);
  signal_phase();
  journal_record record={.id=current.id,.time=now,.held=current.held,.profile=current.profile,
     .sample=sample.filtered,.ambient=ambient_dc,.minimum=current.minimum,.maximum=current.maximum,
     .state=current.state,.why=current.why,.ack_lf=current.why==ABORTED?abort_lf:0};
  bool saved=storage_push(record);
  if (!saved) {
    current.state=INTERRUPTED; current.why=STORAGE_BAD;
    signal_phase();
  }
  bt_log("EV t=%u us=%u id=%u phase=%u why=%u hold=%u queued=%u",
     (unsigned)now,(unsigned)perf_us(),(unsigned)current.id,current.state,current.why,(unsigned)current.held,saved);
}
static void interrupt_batch(reason why) {
  if (!batch_interrupt(&current,why,HAL_GetTick())) return;
  if (why==LID_OPEN) {
    signal_phase();
    uint32_t latency=perf_us()-raw_seen_us;
    metric_add(LID_LAT,latency);
    bt_log("REACT id=%u t=%u latency_us=%u",(unsigned)current.id,(unsigned)HAL_GetTick(),(unsigned)latency);
  }
  transition();
}
void storage_durable(const journal_record *record) {
  if (record->state==RAMP && record->why==NORMAL)
    bt_log("ACK cmd=start id=%u seq=%u",(unsigned)record->id,(unsigned)record->sequence);
  if (record->why==ABORTED) {
    uint32_t latency=perf_us()-record->ack_lf;
    metric_add(ABORT_LAT,latency);
    bt_log("ACK cmd=abort id=%u lat_us=%u seq=%u",(unsigned)record->id,(unsigned)latency,(unsigned)record->sequence);
  }
  if (record->why==POWER_LOSS) {
    current.state=INTERRUPTED; current.why=POWER_LOSS; current.id=record->id;
    current.profile=record->profile; current.held=record->held;
    bt_log("RECOVERY id=%u phase=%u why=%u",(unsigned)current.id,current.state,current.why);
  }
}
static void ambient_read(void) {
  bt_result prev=ambient_result;
  if (!ambient.ready) ambient_result=bmp180_open(&ambient);
  if (ambient.ready) ambient_result=bmp180_measure(&ambient,&ambient_dc,&ut);
  if (ambient_result==BT_OK) ambient_time=HAL_GetTick();
  else { ambient.ready=false; ++error_count; interrupt_batch(AMBIENT_BAD); }
  ++bmp_count;
  if (prev!=ambient_result)
    bt_log("BMP t=%u result=%u temp=%d ut=%u addr=%x",(unsigned)HAL_GetTick(),ambient_result,ambient_dc,ut,ambient.address);
}
static void lid_read(void) {
  uint32_t now=HAL_GetTick();
  bool closed=(GPIOB->IDR&GPIO_PIN_0)==0;
  if (!raw_known || closed!=raw_closed) { raw_known=true; raw_closed=closed; raw_seen_us=perf_us(); }
  bool known=lid.known, previous=lid.closed;
  lid_observe(&lid,closed,now);
  if (lid.known && !lid.closed) interrupt_batch(LID_OPEN);
  if (lid.known && (!known || lid.closed!=previous))
    bt_log("LID t=%u us=%u closed=%u",(unsigned)now,(unsigned)perf_us(),lid.closed);
}
static void sample_task(void) {
  sample_read(); ++adc_count;
  if (ntc_result!=BT_OK) { ++error_count; interrupt_batch(SAMPLE_BAD); }
  if (!ambient_valid(HAL_GetTick())) interrupt_batch(AMBIENT_BAD);
  if (sample.valid && batch_frame(&current,HAL_GetTick(),sample.filtered))
    transition();
}
static void heartbeat(void) {
  bt_log("HEART t=%u us=%u hb=%u phase=%u why=%u n=%d nv=%u a=%d av=%u l=%u j=%u rx=%u err=%u rearm=%u w=%u",
    (unsigned)HAL_GetTick(),(unsigned)perf_us(),(unsigned)++beats,current.state,current.why,
    sample.filtered,sample.valid,ambient_dc,ambient_valid(HAL_GetTick()),lid.known&&lid.closed,
    storage_count(),(unsigned)rx_overflow,(unsigned)rx_errors,(unsigned)rx_rearm,(unsigned)perf_wraps());
}
static void stats(void) {
  static const char *const names[]={"loop","lid","ntc","bmp","rx","sd","heart","adc_gate","lid_period",
    "ntc_period","bmp_period","lid_latency","status_latency","abort_latency"};
  for (unsigned i=0;i<METRIC_COUNT;++i) {
    metric *m=&metrics[i];
    bt_log("MET name=%s n=%u min=%u max=%u sum=%u",names[i],(unsigned)m->n,(unsigned)m->min,
      (unsigned)m->max,(unsigned)m->sum);
  }
  bt_log("TOTAL t=%u us=%u elapsed_us=%u busy_us=%u stack=%u heap=0 alloc_attempts=%u miss_lid=%u miss_ntc=%u miss_bmp=%u",
    (unsigned)HAL_GetTick(),(unsigned)perf_us(),(unsigned)(perf_us()-window_begin),(unsigned)busy_us,perf_stack(),
    (unsigned)heap_attempts,(unsigned)miss_lid,(unsigned)miss_sample,(unsigned)miss_bmp);
  bt_log("COUNTS adc=%u bmp=%u errors=%u cmd=%u rejected=%u rx_overflow=%u rx_errors=%u rx_rearm=%u",
    (unsigned)adc_count,(unsigned)bmp_count,(unsigned)error_count,(unsigned)commands,(unsigned)rejected,
    (unsigned)rx_overflow,(unsigned)rx_errors,(unsigned)rx_rearm);
}
static void deny(const char *cmd,const char *why) {
  ++rejected; bt_log("ERR cmd=%s why=%s t=%u",cmd,why,(unsigned)HAL_GetTick());
}
static bool read_profile(const char *p,thermal_profile *out) {
  long fields[4];
  for (unsigned i=0;i<4;++i) {
    char *end;
    if (*p!=' ') return false;
    fields[i]=strtol(p,&end,10);
    if (p==end || fields[i]<0 || fields[i]>1000) return false;
    p=end;
  }
  if (*p) return false;
  thermal_profile next={(int16_t)fields[0],(int16_t)fields[1],(int16_t)fields[3],(uint16_t)fields[2]};
  if (!profile_valid(next)) return false;
  *out=next; return true;
}
static void command(uint32_t lf) {
  uint32_t now=HAL_GetTick();
  ++commands;
  if (!strcmp(line,"status")) {
    uint32_t latency=perf_us()-lf;
    metric_add(STATUS_LAT,latency);
    bt_log("ACK cmd=status t=%u lat_us=%u id=%u phase=%u why=%u j=%u sd=%u l=%u nv=%u av=%u",
      (unsigned)now,(unsigned)latency,(unsigned)current.id,current.state,current.why,storage_count(),
      storage_ready(),lid.known&&lid.closed,sample.valid,ambient_valid(now));
  } else if (!strcmp(line,"start")) {
    if (batch_active(&current)) deny("start","ACTIVE");
    else if (!lid.known || !lid.closed) deny("start","LID");
    else if (!sample.valid) deny("start","NTC");
    else if (!ambient_valid(now)) deny("start","BMP");
    else if (!storage_ready()) deny("start","SD");
    else if (storage_space()<2) deny("start","FULL");
    else {
      batch_begin(&current,JOURNAL_CAPACITY-storage_space()+1u,profile,now,sample.filtered);
      transition();
    }
  } else if (!strcmp(line,"abort")) {
    if (!batch_active(&current)) deny("abort","IDLE");
    else { abort_lf=lf; interrupt_batch(ABORTED); }
  } else if (!strcmp(line,"profile")) {
    thermal_profile p=batch_active(&current)?current.profile:profile;
    bt_log("PROFILE target=%d band=%d hold_s=%u cool=%d",p.target,p.band,p.hold_s,p.cool);
  } else if (!strncmp(line,"profile ",8)) {
    if (batch_active(&current)) deny("profile","ACTIVE");
    else if (!read_profile(line+7,&profile)) deny("profile","RANGE");
    else bt_log("ACK cmd=profile target=%d band=%d hold_s=%u cool=%d",profile.target,profile.band,profile.hold_s,profile.cool);
  } else if (!strcmp(line,"export")) {
    if (!storage_export()) deny("export","BUSY_OR_SD");
  } else if (!strcmp(line,"nack") || !strcmp(line,"recover")) {
    ambient.address=line[0]=='n'?0x76:0x77;
    ambient.ready=false; due_bmp=now;
    bt_log("FAULT addr=%x t=%u",ambient.address,(unsigned)now);
  } else if (!strcmp(line,"measure")) {
    memset(metrics,0,sizeof(metrics));
    busy_us=miss_lid=miss_sample=miss_bmp=0;
    previous_loop=previous_lid=previous_sample=previous_bmp=0;
    window_begin=perf_us();
    bt_log("WINDOW t=%u us=%u stack=%u alloc_attempts=%u",(unsigned)HAL_GetTick(),
           (unsigned)window_begin,perf_stack(),(unsigned)heap_attempts);
  } else if (!strcmp(line,"stats")) stats();
  else deny("unknown","SYNTAX");
}
static bool serial_poll(void) {
  if (rx_rearm_pending) {
    /* The one-byte ISR can hit the handle lock while foreground starts
     * transmission. Rearm here, outside TX, without racing RX IRQ. */
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    rx_rearm_pending=HAL_UART_Receive_IT(&huart2,&received_byte,1)!=HAL_OK;
    __set_PRIMASK(primask);
  }
  uint32_t loss=rx_overflow+rx_errors+rx_rearm;
  if (loss!=seen_loss) {
    /* Never interpret a truncated command after loss. Flush the pre-loss
     * prefix and discard through the first newly received LF. */
    seen_loss=loss;
    rx_tail=rx_head;
    used=0; discard=true;
  }
  bool work=false;
  for (unsigned budget=0;budget<32 && rx_tail!=rx_head;++budget) {
    uint8_t index=rx_tail,ch=rx_ring[index];
    uint32_t lf=lf_time[index];
    rx_tail=(uint8_t)((index+1u)&127u);
    work=true;
    if (ch=='\n') {
      if (discard) deny("line","OVERFLOW_OR_LENGTH");
      else if (used) { line[used]=0; command(lf); }
      used=0; discard=false;
      break;
    }
    if (ch=='\r' || discard) continue;
    if (used<sizeof(line)-1) line[used++]=(char)ch;
    else discard=true;
  }
  return work;
}
void sensor_bringup(void) {
  perf_start();
  GPIO_InitTypeDef pin={0};
  pin.Pin=GPIO_PIN_0; pin.Mode=GPIO_MODE_INPUT; pin.Pull=GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB,&pin);
  pin.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10;
  pin.Mode=GPIO_MODE_OUTPUT_PP; pin.Pull=GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA,&pin);
  GPIOA->BSRR=(uint32_t)(GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10)<<16;
  adc_init();
  scl(1); sda(1);
  ambient.bus=(bt_bus){NULL,read_reg,write_reg,wait_ms};
  ambient.address=0x77;
  bt_log("BOOT product=BatchTherm lab=3 build=%s core=%u units=dC",BT_BUILD,(unsigned)SystemCoreClock);
  uint32_t now=HAL_GetTick();
  due_lid=due_sample=due_bmp=now;
  due_beat=now+1000;
  storage_init();
}
void sensor_poll(void) {
  uint32_t start=perf_us();
  if (previous_loop) metric_add(LOOP,start-previous_loop);
  previous_loop=start;
  if (release(&due_lid,5,&miss_lid,&previous_lid,LID_PERIOD)) { start=perf_us(); lid_read(); busy(LID_TASK,start); }
  if (release(&due_sample,100,&miss_sample,&previous_sample,SAMPLE_PERIOD)) { start=perf_us(); sample_task(); busy(NTC_TASK,start); }
  if (release(&due_bmp,200,&miss_bmp,&previous_bmp,BMP_PERIOD)) { start=perf_us(); ambient_read(); busy(BMP_TASK,start); }
  uint32_t now=HAL_GetTick();
  if (batch_deadline(&current,now)) transition();
  if (storage_fault()) interrupt_batch(STORAGE_BAD);
  if (!first_frame && lid.known && sample.valid && ambient_valid(now)) {
    first_frame=true;
    bt_log("FIRST t=%u us=%u n=%d a=%d",(unsigned)now,(unsigned)perf_us(),sample.filtered,ambient_dc);
  }
  start=perf_us(); if (serial_poll()) busy(RX_TASK,start);
  start=perf_us(); if (storage_poll()) busy(SD_TASK,start);
  if ((int32_t)(now-due_beat)>=0) {
    due_beat+=1000;
    start=perf_us(); heartbeat(); busy(HEART_TASK,start);
  }
  /* Wokwi C031 stalled at WFI in the capability probe. Keep a polling loop. */
}
