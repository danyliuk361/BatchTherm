#include "storage_bringup.h"
#include "main.h"
#include "sdcard.h"
#include <stdio.h>
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
  card.bus = (sd_bus){NULL, transfer, select_card, now};
  bt_result r = sd_initialize(&card);
  printf("TEST device=SD result=%u ready=%u ocr=%08lx\r\n", r, card.ready,
         (unsigned long)card.ocr);
}
void storage_check(int write_first) {
  /* Destructive only to reserved scratch sector 32, never to saved research
   * images. */
  uint32_t began = HAL_GetTick();
  bt_result r = BT_OK;
  if (write_first) {
    for (unsigned n = 0; n < 512; ++n)
      sector_buffer[n] = (uint8_t)(n * 37u + 19u);
    r = sd_write_sector(&card, 32, sector_buffer);
  }
  if (r == BT_OK)
    r = sd_read_sector(&card, 32, sector_buffer);
  unsigned mismatch = 0;
  for (unsigned n = 0; n < 512; ++n)
    if (sector_buffer[n] != (uint8_t)(n * 37u + 19u))
      ++mismatch;
  printf("STORAGE op=%s sector=32 result=%u mismatch=%u elapsed_ms=%lu\r\n",
         write_first ? "WRITE_READ" : "READ_ONLY", r, mismatch,
         (unsigned long)(HAL_GetTick() - began));
}
