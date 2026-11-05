#include "lab3_io.h"
#include "main.h"
#include "sdcard.h"
#include "sd_stream.h"
static sd_card card;
static uint8_t sector_buffer[512];
static void pause_spi(void) {
  for (volatile unsigned n = 0; n < 20; ++n)
    __NOP();
}
static uint8_t transfer(void *ctx, uint8_t value) {
  (void)ctx;
  uint8_t received = 0;
  for (unsigned n = 0; n < 8; ++n) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5,
                      value & 0x80 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    value <<= 1;
    pause_spi();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
    pause_spi();
    received = (uint8_t)((received << 1) |
                         (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET));
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
  }
  return received;
}
static void select_card(void *ctx, bool selected) {
  (void)ctx;
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,
                    selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
static uint32_t now(void *ctx) {
  (void)ctx;
  return HAL_GetTick();
}
enum { QSIZE=12 };
typedef enum { NONE, SCAN, APPEND, EXPORT } storage_operation;
static sd_stream transfer_job;
static journal_record queue[QSIZE], last;
static unsigned committed, pending, front, export_next, export_end;
static bool scanning, failed, recovering;
static storage_operation operation;
static uint32_t began;

bool storage_fault(void) { return failed; }
unsigned storage_count(void) { return committed; }
unsigned storage_space(void) { return JOURNAL_CAPACITY-committed-pending; }
bool storage_ready(void) { return card.ready && !scanning && !recovering && !failed; }
bool storage_push(journal_record r) {
  if (failed || !card.ready || !storage_space() || pending==QSIZE)
    return false;
  r.sequence=committed+pending+1;
  queue[(front+pending)%QSIZE]=r;
  ++pending;
  return true;
}
bool storage_export(void) {
  if (!storage_ready() || export_next<export_end) return false;
  export_next=0; export_end=committed;
  bt_log("EXPORT begin=%u",export_end);
  if (!export_end) bt_log("EXPORT end=0");
  return true;
}
void storage_init(void) {
  GPIO_InitTypeDef pin = {0};
  pin.Pin = GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_5;
  pin.Mode = GPIO_MODE_OUTPUT_PP;
  pin.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &pin);
  pin.Pin = GPIO_PIN_4;
  pin.Mode = GPIO_MODE_INPUT;
  pin.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &pin);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1 | GPIO_PIN_5, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
  card.bus=(sd_bus){NULL,transfer,select_card,now};
  bt_result result=sd_initialize(&card);
  scanning=result==BT_OK;
  failed=!scanning;
  bt_log("SD init=%u t=%u ocr=%x",result,HAL_GetTick(),(unsigned)card.ocr);
}
static void media_error(bt_result result) {
  failed=true;
  operation=NONE;
  GPIOA->BSRR=(uint32_t)GPIO_PIN_10<<16;
  bt_log("SD fail=%u t=%u committed=%u pending=%u",result,HAL_GetTick(),committed,pending);
}
static void end_scan(void) {
  scanning=false;
  if (last.state==RAMP || last.state==SOAK || last.state==COOL) {
    last.state=INTERRUPTED;
    last.why=POWER_LOSS;
    last.time=HAL_GetTick();
    recovering=storage_push(last);
    if (!recovering) media_error(BT_IO);
  } else bt_log("JOURNAL ready=%u t=%u last_id=%u",committed,HAL_GetTick(),last.id);
}
bool storage_poll(void) {
  if (failed) return false;
  if (operation!=NONE) {
    if (!sd_stream_step(&transfer_job)) return true;
    GPIOA->BSRR=(uint32_t)GPIO_PIN_10<<16;
    storage_operation completed=operation;
    operation=NONE;
    if (transfer_job.result!=BT_OK) {
      media_error(transfer_job.result);
      return true;
    }
    if (completed==APPEND) {
      last=queue[front];
      front=(front+1)%QSIZE; --pending; ++committed;
      bt_log("COMMIT seq=%u id=%u phase=%u why=%u t=%u elapsed_us=%u",
             (unsigned)last.sequence,(unsigned)last.id,last.state,last.why,
             HAL_GetTick(),(unsigned)(perf_us()-began));
      storage_durable(&last);
      if (recovering) {
        recovering=false;
        bt_log("JOURNAL ready=%u t=%u last_id=%u",committed,HAL_GetTick(),last.id);
      }
    } else {
      journal_record r;
      unsigned index=completed==SCAN?committed:export_next;
      bool good=journal_decode(&r,sector_buffer,index+1);
      if (completed==SCAN) {
        if (!good) end_scan();
        else {
          last=r; ++committed;
          if (committed==JOURNAL_CAPACITY) end_scan();
        }
      } else if (!good) {
        bt_log("EXPORT crc_error=%u",index+1);
        export_next=export_end;
      } else {
        bt_log("REC seq=%u id=%u t=%u phase=%u why=%u hold=%u ntc=%d bmp=%d min=%d max=%d p=%d/%d/%u/%d",
               (unsigned)r.sequence,(unsigned)r.id,(unsigned)r.time,r.state,r.why,
               (unsigned)r.held,r.sample,r.ambient,r.minimum,r.maximum,
               r.profile.target,r.profile.band,r.profile.hold_s,r.profile.cool);
        if (++export_next==export_end) bt_log("EXPORT end=%u",export_end);
      }
    }
    return true;
  }
  unsigned index;
  if (scanning) { operation=SCAN; index=committed; }
  else if (pending) {
    operation=APPEND; index=committed;
    journal_encode(sector_buffer,&queue[front]);
  } else if (export_next<export_end) { operation=EXPORT; index=export_next; }
  else return false;
  began=perf_us();
  GPIOA->BSRR=GPIO_PIN_10;
  if (!sd_stream_begin(&transfer_job,&card,JOURNAL_FIRST+index,sector_buffer,operation==APPEND))
    media_error(transfer_job.result);
  return true;
}
