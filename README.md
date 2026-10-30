# BatchTherm

Данилюк Вадим Ігорович, ІО-36. Сенсорна підсистема реєстратора температурного циклу партії. Середовище: Ubuntu 24.04, Docker, STM32 HAL, Wokwi. Плата Nucleo-C031C6, форма B.

## Збірка і перевірка поточної lab2

```sh
cd "/Users/pliffdax/Projects/university/Study/People/Данилюк Вадим Ігорович/2026-Fall/SP/Lab01/Work/BatchTherm"
make build
make test
python3 tools/verify_evidence.py
```

Docker image за замовчуванням `batchtherm-build:lab2`; ELF/HEX/BIN у firmware/build. Для Wokwi відкрити цей корінь у VS Code і виконати Wokwi: Start Simulator. Існуючу ліцензію зберігати, trial не вмикати. Гілка lab1 містить окремий UART/LED bring-up; у ній image `batchtherm-build:lab1`, а у lab2 PA5 позначає спільну валідність та входження в коридор58–62 °C.

## Входи і команди

BMP180/I²C - температура середовища; NTC/ADC - температура зразка; контакт/GPIO - кришка. У Wokwi натиснута синя кнопка=CLOSED. Температури двох моделей незалежні. UART: selftest, capture, status, nack, recover, counts, sdwrite, sdread. Значення в0.1 °C, використовувати лише valid=1. `sdwrite` змінює scratch-сектор32 поточного demo image; saved evidence не використовувати як writable media.

[PRD](docs/PRD.md), [компоненти](docs/hardware-components.md), [pinout](docs/pinout.md), [архітектура](docs/architecture.md), [аналіз власних VCD](docs/captures/analysis.md), [план ЛР3–4](docs/tasks.md), [походження заготовки](docs/starter-template.md). PDF у Reports.

Host line coverage: BMP18097.73%, NTC100%, App100%, SD90.10%; це не покриття фізичного стенда. CI workflow додано, remote run до успішного push не підтверджений. diagram.json записує I²C/UART/ADC/контакт; diagram.storage.json змінює тільки канали аналізатора для SPI. Виміряні ELF/HEX/BIN у docs/evidence/lab2, hashes у run.json/manifest. Повторну перевірку артефактів ЛР1 виконує `python3 tools/check_lab1.py`.

Автомат партій, продуктові profile/start/abort/export, журнал із CRC/power-loss recovery, S1/S2/S3 baseline та FreeRTOS належать наступним лабораторним і ще не реалізовані. Фактичні відео й зовнішні кроки здачі обліковуються в локальному GUIDE поза репозиторієм.
