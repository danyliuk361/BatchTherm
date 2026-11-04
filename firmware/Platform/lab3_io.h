#ifndef BATCHTHERM_LAB3_IO_H
#define BATCHTHERM_LAB3_IO_H
#include "batch.h"
#include <stdint.h>
void bt_log(const char *format, ...);
void perf_start(void);
uint32_t perf_us(void);
uint32_t perf_wraps(void);
void perf_paint(void);
unsigned perf_stack(void);
void storage_init(void);
bool storage_poll(void);
bool storage_ready(void);
unsigned storage_count(void);
unsigned storage_space(void);
bool storage_push(journal_record r);
bool storage_export(void);
bool storage_fault(void);
void storage_durable(const journal_record *r);
#endif
