from pathlib import Path
for folder in ("App","Devices"):
 for f in (Path("firmware")/folder).glob("*.[ch]"):
  text=f.read_text()
  assert "stm32" not in text.lower() and "HAL_" not in text and '#include "board.h"' not in text, f
print("Layer dependencies: PASS")
