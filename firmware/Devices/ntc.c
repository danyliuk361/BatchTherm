#include "devices.h"
#include <math.h>
bt_result ntc_temperature(uint16_t adc, int16_t *decic) {
  if (adc <= 8 || adc >= 4087)
    return BT_RANGE;
  /* Fixed 10k to 3V3, NTC to GND. Rntc/R25 = ADC/(4095-ADC). */
  float inverse =
      logf((float)adc / (4095.0f - (float)adc)) / 3977.0f + 1.0f / 298.15f;
  float celsius = 1.0f / inverse - 273.15f;
  if (!isfinite(celsius) || celsius < 0.0f || celsius > 85.0f)
    return BT_RANGE;
  *decic = (int16_t)lroundf(celsius * 10.0f);
  return BT_OK;
}
