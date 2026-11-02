#ifndef BATCHTHERM_BATCH_H
#define BATCHTHERM_BATCH_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { IDLE, RAMP, SOAK, COOL, DONE, INTERRUPTED } phase;
typedef enum {
  NORMAL, LID_OPEN, SAMPLE_BAD, AMBIENT_BAD, ABORTED, RAMP_TIMEOUT,
  COOL_TIMEOUT, POWER_LOSS, JOURNAL_FULL, STORAGE_BAD, FRAME_GAP, OUT_OF_BAND
} reason;
typedef struct {
  int16_t target, band, cool;
  uint16_t hold_s;
} thermal_profile;
typedef struct {
  uint32_t id, entered, previous, held;
  thermal_profile profile;
  int16_t minimum, maximum;
  phase state;
  reason why;
  bool have_frame;
} batch;
typedef struct {
  uint32_t sequence, id, time, held;
  thermal_profile profile;
  int16_t sample, ambient, minimum, maximum;
  phase state;
  reason why;
  uint32_t ack_lf; /* RAM only; never encoded on the card. */
} journal_record;
enum { JOURNAL_FIRST = 128, JOURNAL_CAPACITY = 512, RECORD_BYTES = 64 };
bool profile_valid(thermal_profile p);
bool batch_active(const batch *b);
void batch_begin(batch *b, uint32_t id, thermal_profile p, uint32_t now, int16_t sample);
bool batch_interrupt(batch *b, reason why, uint32_t now);
bool batch_frame(batch *b, uint32_t now, int16_t sample);
bool batch_deadline(batch *b, uint32_t now);
void journal_encode(uint8_t sector[512], const journal_record *r);
bool journal_decode(journal_record *r, const uint8_t sector[512], uint32_t sequence);
#endif
