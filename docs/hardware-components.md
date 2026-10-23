# Компоненти BatchTherm

## Первинні документи

| Пристрій | Документ і точний розділ | Рішення |
|---|---|---|
| STM32C031C6 | [ST DS13867](https://www.st.com/resource/en/datasheet/stm32c031f4.pdf), pin definitions / alternate function mapping; [RM0490](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), RCC/I2C/ADC/USART | Cortex-M0+, 48 MHz, 32 KiB flash, 12 KiB SRAM; не застосовуємо Cortex-M4/DWT приклади |
| NUCLEO-C031C6 | [UM2953 Rev2](https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf), §6.4 power, §6.7 LEDs, §6.11 connectors / рис. pinout, с.19 | PA5 LED, PA2/PA3 VCP; SWD PA13/PA14 зберігаються |
| BMP180 | Bosch BST-BMP180-DS000-09, [PDF виробника у дистриб’ютора](https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf), с.6–7 електрика, с.11–15 алгоритм, с.18 map, с.19–21 I²C, с.22–23 pinout/application | VDD 1.8–3.6 V, VDDIO 1.62–3.6 V; живлення 3.3 V. I²C 7 bit 0x77, ID D0=55; F4=2E запускає температуру, ≥4.5 мс очікування, F6/F7 UT; AA–BF калібрування. Струм залежить від режиму: для бюджету резервуємо 1 mA проти типового peak 650 μA |
| NTC | [Vishay NTCLE100E3, doc29049](https://www.vishay.com/docs/29049/ntcle100.pdf), с.1 властивості, с.2 electrical/order table, с.4 R(T) | Аналоговий дільник; регістрів і chip ID немає. Фізичний кандидат 10 kΩ має B=3977 K; модель Wokwi за замовчуванням B=3950 K, тому задаємо beta=3977 явно. Резистор 10 kΩ до 3V3, NTC до GND; максимальний струм дільника <0.33 mA, максимум потужності NTC 0.272 mW при R=10 kΩ |
| Контакт | [Omron D2F datasheet](https://components.omron.com/sites/default/files/datasheet_pdf/B036-E1.pdf), terminals/contact ratings | Пасивний dry contact, у моделі pushbutton. Один вивід PB0, другий GND, pull-up 10 kΩ до 3V3; замкнений контакт 0.33 mA. Немає register map. Debounce у ПЗ, силові кола не підключаються |

Для фізичного NTC потрібне калібрування і перевірка фактичного B та допуску резистора. Наявність іншого NTC в моделі не встановлює точність фізичного зразка. Пряме підключення 5-V аналогового виходу до ADC заборонене.

## Відповідність моделей

[BMP180](https://docs.wokwi.com/parts/board-bmp180): VCC/3.3V, GND, SCL, SDA; [NTC](https://docs.wokwi.com/parts/wokwi-ntc-temperature-sensor): VCC/GND/OUT, модель дільника 10k/10k; [button](https://docs.wokwi.com/parts/wokwi-pushbutton): GPIO і брязкіт; [board](https://docs.wokwi.com/parts/board-st-nucleo-c031c6); [analyzer](https://docs.wokwi.com/parts/wokwi-logic-analyzer). Виходи температур не пов’язані фізично. Симулятор не перевіряє самонагрів, точність опорної напруги, струм регулятора і RC фронти.

## Постійна пам’ять наступного етапу

[Wokwi microSD](https://docs.wokwi.com/parts/wokwi-microsd-card), SPI mode 0, живлення 3.3 V, CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7: PA5 зайнятий бортовим LED, тому для bring-up обираємо PB3/PB4/PB5 (SPI1 AF0), CS=PB1. Стартова частота ≤400 kHz, після ініціалізації ≤3 MHz. Протокол: [SD Association simplified specifications](https://www.sdcard.org/downloads/pls/), Physical Layer Simplified Specification, SPI mode. Фізична карта має peak до сотень mA: окреме стабільне 3.3-V джерело з bypass, спільна земля; 3V3 Nucleo для цього не обіцяємо. Спочатку перевіряється raw сектор без файлової системи. Параметри і межі запису з’ясовуються bring-up, не штучною затримкою.
