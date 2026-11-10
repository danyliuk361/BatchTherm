# BatchTherm

Данилюк Вадим Ігорович, ІО-36. Реєстратор температурного циклу партії. Nucleo-C031C6, Ubuntu 24.04, Docker, STM32 HAL, Wokwi.

## Збірка і запуск ЛР3

З кореня BatchTherm:

```sh
make build BUILD_DIR=build/clock BUILD_ID=f6b5827-0ba44211a1db
make test
shasum -a 256 firmware/build/clock/firmware.{elf,hex,bin}
```

Для повторення вимірів звірити хеші з [ідентифікатором виміряної прошивки](docs/evidence/lab3/identity.json). Звичайний `wokwi.toml` використовує виміряний ELF/HEX з `docs/evidence/lab3` і записує живий VCD в окремий файл. У VS Code з кореня проєкту виконати `Wokwi: Start Simulator`. Для чистої демонстрації SD використовувати копію порожнього образу; підтверджені журнали не монтувати на запис.

## Входи і команди

BMP180/I²C — температура середовища; NTC/ADC — температура зразка; контакт/GPIO — кришка. У Wokwi зафіксована синя кнопка означає CLOSED, а температури моделей незалежні. PA5 світиться у SOAK. UART 115200 8N1: `profile`, `start`, `abort`, `status`, `export`. Одиниці профілю й температури — 0,1 °C, час витримки в команді profile — секунди. `measure` і `stats` керують статистичним вікном; `nack`/`recover` — діагностика BMP. Прийняті start/abort підтверджуються після запису в журнал.

[PRD](docs/PRD.md), [задачі ЛР3](docs/lab3-tasks.md), [розводка](docs/pinout.md), [звіт і дані](docs/lab3-report.md), [базова лінія ЛР4](docs/baseline-lab4.md), [обмеження](docs/limitations.md). S2: один запуск 3 602 000 мс MCU, 30 партій і 120 підтверджених CRC-записів. Кришка реагувала до 89,466 мс у S3, але 5-мс релізи пропускалися; стійкий ACK abort досяг 1734,645 мс при межі 150 мс. Сценарії, вердикти й сирі дані збережено з виміряною збіркою. FreeRTOS належить ЛР4.

Історична ревізія `lab2` містить bring-up сенсорів і scratch-sector SD; її команди та [архітектура ЛР2](docs/architecture.md) описують саме ту версію. Попередні виміри збережені в `docs/metrics/lab1` і `docs/metrics/lab2`.
