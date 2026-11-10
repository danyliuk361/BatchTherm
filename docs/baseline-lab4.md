# Базова лінія BatchTherm для ЛР4

Збірка `f6b5827-0ba44211a1db`, Nucleo-C031C6, 48 МГц, Ubuntu 24.04, Docker, STM32 HAL, Wokwi 4.1.0. Точний [identity.json](evidence/lab3/identity.json) зберігається з ELF/HEX/BIN. ЛР4 повторює цю версію і FreeRTOS в один день, на одному стенді, з перевіркою кожної [гіпотези](limitations.md).

З кореня BatchTherm:

```sh
make build BUILD_DIR=build/clock BUILD_ID=f6b5827-0ba44211a1db
make test
shasum -a 256 firmware/build/clock/firmware.{elf,hex,bin}
```

Точні байти залежать від зафіксованого образу Docker і шляху збірки `/workspace`; ідентичність визначає SHA-256. Звичайний `wokwi.toml` посилається на виміряні ELF/HEX і пише новий VCD окремо від збережених.

Для автоматизації потрібні Node.js, Playwright Chromium, Python 3 і чинна ліцензія Wokwi. Шляхи можна перевизначити через `PLAYWRIGHT_MODULE` і `CHROME_EXECUTABLE`. Запускати з кореня репозиторію, кожен раз у новий каталог. Порожня SD — 2 MiB; для S3, abort, load — початковий образ зі справними 64 секторами, для full — 512.

```sh
mkdir -p /tmp/batchtherm-lab3
python3 tools/lab3_fixture.py 64 /tmp/batchtherm-lab3/fixture-64.img
python3 tools/lab3_fixture.py 512 /tmp/batchtherm-lab3/fixture-512.img
FIRMWARE_DIR=docs/evidence/lab3 node tools/lab3_run.cjs s1 /tmp/batchtherm-lab3/s1
FIRMWARE_DIR=docs/evidence/lab3 SD_IMAGE=/tmp/batchtherm-lab3/fixture-64.img node tools/lab3_run.cjs s3 /tmp/batchtherm-lab3/s3
FIRMWARE_DIR=docs/evidence/lab3 SD_IMAGE=/tmp/batchtherm-lab3/fixture-64.img node tools/lab3_run.cjs s3-abort /tmp/batchtherm-lab3/abort
python3 tools/lab3_analyze.py /tmp/batchtherm-lab3/s3
python3 tools/lab3_uart_vcd.py /tmp/batchtherm-lab3/s3
```

Для S2 використати виміряний каталог із `identity.json`, `firmware.elf/.hex/.bin` та `sd-before.img`:

```sh
python3 tools/lab3_collect.py /tmp/batchtherm-lab3/S2 /path/to/frozen
```

`HEART`: t (мс), us (TIM3), hb, фаза, валідність, sequence, втрати UART, оберти. `measure` відкриває вікно; `stats` дає MET, TOTAL, COUNTS. UART 115200 8N1; PA2/PA3 UART, PA8 ADC, PB0 контакт, PA9 активність, PA10 SD, PA5 SOAK. Для S2 знімати PA5, PA8, PB0, PA9, PA10; UART зберігається окремо. Див. [задачі](lab3-tasks.md) та [розводку](pinout.md).

| Сценарій | Умова | Доказ |
|---|---|---|
| S1 | 60 с idle, NTC 25 °C, BMP 24 °C, кришка закрита, SD порожня | `s1/metrics.json` |
| S2 | 3600 с MCU, партії кожні 120 с, NTC 60 °C через 8 с і 25 °C через 55 с, status кожні 10 с | `S2/S2.result.json`, UART/VCD |
| S3 | export 64 секторів, 36 відкриттів зі змінним зсувом, 10 status/с, ≥60 с MCU | `s3/metrics.json`, `s3/uart-wire.json` |
| Abort | Той самий образ і потік status, 30 прийнятих abort, ACK після COMMIT | `abort/metrics.json` |
| Load | `load`, `RUN_MS=12000`, `RATE=2`, 10, 20, 50; `FLOOD=1`, `RUN_MS=6000`, `RATE=10` — burst 3500 Б | `load-*/metrics.json`, `flood/metrics.json` |
| Fault/startup/nosd | `fault`: NACK/повернення, SDA до GND на 7 с, NTC −5 °C; `startup`: SDA затиснута до спроби start; `nosd`: SD відсутня | `fault`, `startup`, `nosd` |
| Межі | `probe` (5 с, 58/62/63 °C та довгий рядок), `timeouts` (180 с у RAMP і COOL), `full` (образ 512 записів) | `functional`, `timeouts`, `full` |
| Перезапуск | `cut`: PA10 високий, немає COMMIT abort; `recover`: `SD_IMAGE=cut/sd-after.img` | `cut`, `recover` |

Шляхи в колонці «Доказ» — від `docs/metrics/lab3`; захоплення в `docs/captures/lab3`. Перед порівнянням звірити реальні темпи команд по RX, інтервали і стани. На платі для brownout окремим входом зафіксувати падіння живлення SD.

| Метрика | S1 | S2 | S3 |
|---|---:|---:|---:|
| Корисна зайнятість, % | 18,489 | 20,171 | 98,784 |
| Максимум періоду циклу, мс | 43,848 | 50,829 | 78,781 |
| Пропущено кришка / NTC / BMP | 1629 / 0 / 0 | 106451 / 0 / 0 | 11819 / 0 / 0 |
| Стек, Б | 712 | 712 | 812 |
| Heap, Б | 0 | 0 | 0 |
| Максимум зовнішньої реакції кришки, мс | — | — | 89,466 |
| Максимум ACK status, мс (TIM3) | — | 0,603 | 64,217 |
| Найбільша похибка SOAK, мс MCU / PA5 | — | 68 / 67,331 | — |

Пам'ять ARM: flash 27616 Б, RAM за linker 5024 Б. Серія abort: мін./сер./макс. 1506,953 / 1610,707 / 1734,645 мс. Status у тій серії до 47,395 мс; VCD у S3 до 64,910 мс. 50 команд/с — перша виміряна точка зламу (між 20 і 50 у цьому ряду); максимум 363,377 мс і 50 переповнених байтів. Затиснена SDA: 34 спроби по 728 мкс; після відпускання BMP відновився. Затриманий запуск BMP — спільний кадр через 1126 мс від boot. Повні min/avg/max, кількість, джитер і пік пам'яті є у JSON/MET кожного сценарію.

S2: 3602000 мс неперервно, 30 DONE, 120 записів із валідним CRC, 359 status, максимальний проміжок heartbeat 1001 мс, heap не зростав. SOAK вимірювати за парою фронтів PA5 усередині вікна й активної партії PA9. Початковий імпульс до RAMP лишається у сирому VCD, але не зараховується до SOAK.
