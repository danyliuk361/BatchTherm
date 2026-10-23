#include "board.h"
#include "devices.h"
#include "main.h"
#include "quality.h"
#include "storage_bringup.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart2;
static ADC_HandleTypeDef adc;
static bmp180 ambient;
static sample_filter sample;
static lid_filter lid;
static bt_result ambient_result = BT_CAL, ntc_result = BT_CAL;
static int16_t ambient_dc, sample_dc;
static uint16_t adc_raw, ut;
static uint32_t ambient_time, ambient_due, sample_due, lid_due, log_due;
static uint32_t adc_count, bmp_count, error_count, command_count;
static uint8_t received_byte;
static volatile uint8_t rx_ring[128], rx_head, rx_tail;
static volatile uint32_t rx_overflow, rx_errors;
static char line[80];
static unsigned used;
static int discard;
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle) {
  if (handle != &huart2)
    return;
  uint8_t next = (uint8_t)((rx_head + 1u) & 127u);
  if (next == rx_tail)
    ++rx_overflow;
  else {
    rx_ring[rx_head] = received_byte;
    rx_head = next;
  }
  (void)HAL_UART_Receive_IT(&huart2, &received_byte, 1);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
  if (handle == &huart2) {
    ++rx_errors;
    (void)HAL_UART_Receive_IT(&huart2, &received_byte, 1);
  }
}
static const char *result_name(bt_result r) {
  static const char *names[] = {"OK", "NACK", "TIMEOUT", "IO",
                                "ID", "CAL",  "RANGE"};
  return names[r];
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
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_StatusTypeDef r = HAL_ADC_Start(&adc);
  if (r == HAL_OK)
    r = HAL_ADC_PollForConversion(&adc, 5);
  if (r == HAL_OK) {
    adc_raw = (uint16_t)HAL_ADC_GetValue(&adc);
    ntc_result = ntc_temperature(adc_raw, &sample_dc);
  } else
    ntc_result = r == HAL_TIMEOUT ? BT_TIMEOUT : BT_IO;
  (void)HAL_ADC_Stop(&adc);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
  sample_observe(&sample, ntc_result, sample_dc);
  ++adc_count;
  if (ntc_result != BT_OK)
    ++error_count;
}
static void ambient_read(void) {
  bt_result previous = ambient_result;
  if (!ambient.ready)
    ambient_result = bmp180_open(&ambient);
  if (ambient.ready)
    ambient_result = bmp180_measure(&ambient, &ambient_dc, &ut);
  if (ambient_result == BT_OK)
    ambient_time = HAL_GetTick();
  else {
    ambient.ready = false;
    ++error_count;
  }
  ++bmp_count;
  if (previous != ambient_result)
    printf("BUS t=%lu addr=%02x status=%s\r\n", (unsigned long)HAL_GetTick(),
           ambient.address, result_name(ambient_result));
}
static void status(void) {
  uint32_t now = HAL_GetTick();
  printf("FRAME t=%lu adc=%u sample_dC=%d median_dC=%d sample_valid=%u "
         "ambient_dC=%d ambient_valid=%u ut=%u lid=%s usable=%u band=%u\r\n",
         (unsigned long)now, adc_raw, sample_dc, sample.filtered, sample.valid,
         ambient_dc,
         ambient_result == BT_OK && (uint32_t)(now - ambient_time) <= 600, ut,
         !lid.known   ? "UNKNOWN"
         : lid.closed ? "CLOSED"
                      : "OPEN",
         thermal_usable(&sample, &lid, ambient_result, now - ambient_time),
         thermal_in_band(&sample, 600, 20));
}
static void selftest(void) {
  ambient.ready = false;
  ambient_result = bmp180_open(&ambient);
  printf("TEST device=BMP180 addr=%02x id=%02x result=%s ac5=%u ac6=%u mc=%d "
         "md=%d\r\n",
         ambient.address, ambient.id, result_name(ambient_result), ambient.ac5,
         ambient.ac6, ambient.mc, ambient.md);
  sample_read();
  printf("TEST device=NTC adc=%u sample_dC=%d result=%s\r\n", adc_raw,
         sample_dc, result_name(ntc_result));
  printf("TEST device=LID raw_closed=%u known=%u result=OK\r\n",
         HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET, lid.known);
}
static void command(void) {
  ++command_count;
  if (!strcmp(line, "sdwrite"))
    storage_check(1);
  else if (!strcmp(line, "sdread"))
    storage_check(0);
  else if (!strcmp(line, "selftest") || !strcmp(line, "capture"))
    selftest();
  else if (!strcmp(line, "status"))
    status();
  else if (!strcmp(line, "nack")) {
    ambient.address = 0x76;
    ambient.ready = false;
    ambient_read();
  } else if (!strcmp(line, "recover")) {
    ambient.address = 0x77;
    ambient.ready = false;
    ambient_read();
  } else if (!strcmp(line, "counts"))
    printf("COUNTS adc=%lu bmp=%lu errors=%lu commands=%lu rx_overflow=%lu "
           "rx_errors=%lu\r\n",
           (unsigned long)adc_count, (unsigned long)bmp_count,
           (unsigned long)error_count, (unsigned long)command_count,
           (unsigned long)rx_overflow, (unsigned long)rx_errors);
  else
    printf("ERR command=UNKNOWN scope=LAB2\r\n");
}
void sensor_bringup(void) {
  GPIO_InitTypeDef pin = {0};
  pin.Pin = GPIO_PIN_0;
  pin.Mode = GPIO_MODE_INPUT;
  pin.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &pin);
  pin.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  pin.Mode = GPIO_MODE_OUTPUT_PP;
  pin.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &pin);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
  adc_init();
  pin.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  pin.Mode = GPIO_MODE_OUTPUT_OD;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &pin);
  scl(1);
  sda(1);
  ambient.bus = (bt_bus){NULL, read_reg, write_reg, wait_ms};
  ambient.address = 0x77;
  printf("BOOT product=BatchTherm lab=2 units=dC adc_bits=12 core_hz=%lu\r\n",
         (unsigned long)SystemCoreClock);
  selftest();
  ambient_read();
  storage_init();
  uint32_t now = HAL_GetTick();
  ambient_due = now + 200;
  sample_due = now + 100;
  lid_due = now;
  log_due = now + 200;
}
void sensor_poll(void) {
  uint32_t now = HAL_GetTick();
  if ((int32_t)(now - lid_due) >= 0) {
    lid_due = now + 5;
    bool old = lid.closed, known = lid.known;
    lid_observe(&lid, HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET,
                now);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,
                      lid.known && lid.closed ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (lid.known && (!known || old != lid.closed))
      printf("LID t=%lu closed=%u\r\n", (unsigned long)now, lid.closed);
  }
  if ((int32_t)(now - sample_due) >= 0) {
    sample_due += 100;
    sample_read();
  }
  if ((int32_t)(now - ambient_due) >= 0) {
    ambient_due += 200;
    ambient_read();
  }
  if ((int32_t)(now - log_due) >= 0) {
    log_due += 200;
    status();
  }
  now = HAL_GetTick();
  HAL_GPIO_WritePin(
      GPIOA, GPIO_PIN_5,
      thermal_usable(&sample, &lid, ambient_result, now - ambient_time) &&
              thermal_in_band(&sample, 600, 20)
          ? GPIO_PIN_SET
          : GPIO_PIN_RESET);
  uint8_t ch;
  if (rx_tail != rx_head) {
    ch = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & 127u);
    if (ch == '\r' || ch == '\n') {
      if (!discard && used) {
        line[used] = 0;
        command();
      }
      used = 0;
      discard = 0;
    } else if (used < sizeof(line) - 1 && !discard)
      line[used++] = (char)ch;
    else {
      discard = 1;
      used = 0;
    }
  }
}
