# Перевірка обсягу ЛР1–2

Фактичні досліди: 06.11.2026. Форма B: Ubuntu 24.04, Docker, STM32 HAL, Wokwi. Збережені raw UART, VCD, образи SD та ELF/HEX/BIN ідентифікують виміряні версії. Host-тести, ARM build і симуляція є різними рівнями доказів.

| Вимога | Статус і доказ |
|---|---|
| ЛР1: продукт, FR, компоненти, план | Перевірено: docs/PRD.md, hardware-components.md; датчики не виконуються в гілці lab1 |
| ЛР1: Docker, MCU/HAL, .ioc, build | Перевірено: дві чисті збірки з однаковими трьома SHA-256; docs/metrics/lab1/reproducibility.json |
| ЛР1: UART і LED | Перевірено в Wokwi: ready/tick0…6, незалежне UART decode, 6 інтервалів LED1000.978–1001.482 мс; signals.json і captures/lab1 |
| ЛР2: pinout, конфлікти, електрика, clocks | Перевірено документально: docs/pinout.md; струм розрахунковий, фізичний не виміряний |
| ЛР2: усі входи окремо і разом | Перевірено: BMP180 ID/calibration/UT, NTC ADC/одиниці, контакт; functional-final, lid-final та integration-final |
| ЛР2: self-test, units, фільтр, пороги | Перевірено: кадри dC/valid, median3, debounce25 мс, inclusive58/62 °C, range0…85 °C; host boundary tests і Wokwi |
| ЛР2: error/recovery | Перевірено: відсутня адреса76 дає NACK, recover77 відновлює ID/calibration; invalid NTC очищує median |
| ЛР2: I²C/UART/SPI/ADC capture | Перевірено: captures/lab2/*.vcd, текстовий sigrok decode, ручні I²C біти, часові параметри в analysis.md; GPIO marker для ADC |
| ЛР2: запис SPI і перевірка command order | Перевірено для SDHC: CMD0/8/55/41/58/24/17, sector32,512 байтів; NOR WREN/JEDEC не застосовуються |
| ЛР2: збереження поза RAM | Перевірено: збережений образ прочитаний новим процесом без write; persistent-read-final, mismatch0 |
| ЛР2: шарування і component diagram | Перевірено: Core/Platform/Devices/App, check_layers.py, docs/architecture.md і figures/components.png |
| ЛР2: dependencies, warnings, host-тести | Перевірено: vendored hashes/version, ARM -Wall -Wextra без warnings, make test; BMP97.73%,NTC100%,App100%,SD90.10% lines |
| CI на PR | Workflow реалізований; remote execution залишається неперевіреним до публікації |
| Звіти | PDF ЛР1 на5 сторінках, ЛР2 на9; титули, таблиці, усі сторінки візуально перевірені; без номерів і колонтитулів |
| Фізичне обладнання | Недоступне у формі B; фронти, струм, абсолютна точність та фізична power-loss atomicity не підтверджені |
| Підготовка ЛР3–4 | docs/tasks.md: періоди/deadlines, журнал CRC/recovery, S1/S2/S3, вимірювання, same-day порівняння й6 RTOS дослідів; реалізація не виконувалася |

Повільний GPIO backend і довгі SD виклики є виміряними обмеженнями. ЛР2 не доводить виконання майбутніх deadline/FR автомата партій. One-hour S2, статистика≥30 подій, OS costs і power-loss journal належать наступним лабораторним. Статус зовнішніх кроків здачі ведеться в локальному GUIDE поза репозиторієм.
