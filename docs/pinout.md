# BatchTherm: підключення ЛР2

Плата Nucleo-C031C6, STM32C031C6 LQFP48. Вимкнути живлення перед фізичним монтажем, спільна GND, усі цифрові й аналогові входи 3.3 V. diagram.json - схема форми B, Platform - єдине місце pin definitions. Контакт тут моделює кнопка: натиснута = CLOSED; відпущена = OPEN.

| MCU / вивід корпусу | Порт плати | Режим / AF | Пристрій | Pull / живлення | Підстава |
|---|---|---|---|---|---|
| PA0 / 11 | CN8.1 A0; morpho PA0 | ADC_IN0, analog | NTC OUT | 0–3.3 V; дільник 10k/10k | DS13867 табл.12 с.30–34; UM2953 табл.11 |
| PB0 / 19 | CN5.3 D10; morpho PB0 | GPIO input, AF не потрібна | контакт 1.l | 10k до 3V3 і internal pull-up; 2.l до GND | DS13867 табл.12; UM2953 табл.11 |
| PB8 / 47 | CN5.10 D15/SCL; morpho PB8 | GPIO OD LOW / input release; AF6 доступна, не обрана | BMP180 SCL | 4.7k до 3V3 + internal pull-up | DS13867 табл.15 с.36; UM2953 табл.11 |
| PB9 / 48 | CN5.9 D14/SDA; morpho PB9 | GPIO OD LOW / input release; AF6 доступна, не обрана | BMP180 SDA | 4.7k до 3V3 + internal pull-up | DS13867 табл.15; UM2953 табл.11 |
| PA2 / 13 | morpho PA2 | USART2_TX AF1 | UART RX монітора | 3.3 V push-pull | DS13867 табл.13 с.35; UM2953 табл.12 |
| PA3 / 14 | morpho PA3 | USART2_RX AF1 | UART TX монітора | 3.3 V | Ті самі документи |
| PA5 / 16 | CN5.6 D13 | GPIO output | вбудований LED | штатна обв’язка | UM2953 §6.7 |
| PA8 / 28 | morpho PA8 | GPIO output | analyzer ADC_MARK | маркер обрамлює ADC conversion | DS13867 табл.12 |
| PA9 / 29 | CN9 D8 / morpho PA9 | GPIO output | analyzer LID_DB | фільтрований контакт | DS13867 табл.12; UM2953 табл.12 |
| PB1 / 20 | CN8.4 A3 / morpho PB1 | GPIO output | microSD CS | HIGH між транзакціями | DS13867 табл.12 |
| PB3 / 42 | D3 / morpho PB3 | GPIO output; SPI1 AF0 доступна | microSD SCK | SPI mode0, idle LOW | DS13867 табл.15 |
| PB4 / 43 | D5 / morpho PB4 | GPIO input; SPI1 AF0 доступна | microSD DO/MISO | input pull-up | DS13867 табл.15 |
| PB5 / 44 | D6 / morpho PB5 | GPIO output; SPI1 AF0 доступна | microSD DI/MOSI | idle HIGH | DS13867 табл.15 |

Для фізичного morpho використати підпис PAx/PBx на рисунку 7 UM2953 с.19 і звірити ревізію плати. Номери LQFP у першій колонці не є номерами контактів CN7/CN10. У UM2953 Rev2 табл.12 наприкінці має суперечливі позначення PB8/PB9; функції SCL/SDA звірено за DS13867 табл.15 і табл.11 UM2953. Для Wokwi використовуються імена MCU, тому суперечність Arduino-підписів не змінює з’єднання.

## Адреси, схема і порядок

BMP180: 7-bit 0x77, D0=0x55, калібрування AA–BF, UT F6/F7; I²C без інших slave. Спеціально відсутня 0x76 потрібна для NACK. NTC/контакт chip ID не мають. SDHC: SPI CS PB1, CMD8 echo 0x1AA, CMD58 OCR.CCS=1; JEDEC ID SPI-NOR не застосовується.

З’єднати VCC BMP180 і NTC з 3V3, GND обох і аналізатора з GND. Виходи SCL/SDA/OUT підключити до відповідних рядків. Додати pull-up 4.7k; з’ясувати на фізичному breakout, чи вже є резистори: два паралельні 4.7k дають 2.35k. Контакт між PB0 і GND, окремий pull-up 10k. Не підключати 5-V вихід до PA0. Живлення карти: у Wokwi 3V3; на фізичному стенді окремий регулятор з common GND і bypass за специфікацією конкретної карти.

## Частоти і конфлікти

SYSCLK=HSI 48 MHz, AHB/APB divider1; USART2 48 MHz/приблизно417 =115108 baud, номінал115200, 8N1. ADC=PCLK/4=12 MHz, sampling79.5+conversion12.5 циклів ≈7.67 μs без HAL overhead. Реальний інтервал ADC задається MCU clock100 мс, а VCD фіксує рамку виконання через PA8.

I²C та SPI є програмними поверх HAL GPIO; prescaler периферії не використовується. Частота визначається loop delay та HAL overhead; порівняння з 100-kHz upper limit I²C і 400-kHz SD initialization наведено у captures/analysis.md. Їх CPU cost вимірюється у ЛР3, де не слід видавати loop delay за вільний CPU.

SWD PA13/PA14 збережено. PA5 не використовується як SPI SCK, щоб не конфліктувати з LED. PB0 не використовується як NSS, оскільки це контакт кришки. Таймери не зайняті GPIO SPI/I²C; TIM3 залишений для вимірювання у ЛР3. DMA не використовується. Камера у формі B не потрібна.

## Бюджет 3V3

BMP180 до1 mA peak, NTC≤0.33 mA, контакт≤0.33 mA, обидва I²C pull-up разом до1.4 mA при LOW, internal pull-up додають менше0.2 mA. Сума входів <3.3 mA без плати/LED/SD. Джерело - Nucleo USB→3V3; цей консервативний розрахунок залишає значний запас за умови успішної USB enumeration: UM2953 §6.5.2 обмежує суму плати й shield до500 mA. Резерв =500 mA − Iплати −3.3 mA; точний залишок невідомий без виміру Iплати. Фактичне споживання регулятора і падіння напруги в симуляторі не виміряні. Для SD резервуємо окреме джерело3.3 V ≥250 mA з урахуванням конкретної карти; на Nucleo 3V3 такий peak не покладаємо.

## Відхилені варіанти

1. SPI1 PA5/PA6/PA7: PA5 вже з’єднаний з LD4; перемикання світлодіода спотворило б clock, а SPI світило б LED. Вибрано PB3/4/5 без перепідключення LED.
2. CS SD на PB0: пін потрібний контакту кришки; обрано PB1.
3. Два термометри на одній цифровій шині: прибирає ADC і не відповідає трьом різним інтерфейсам. NTC лишається аналоговим.
4. Hardware I2C1 AF6 на PB8/9: правильний pin mapping, але у фактичному стенді Wokwi/HAL були помилки завершення; збережені сирі записи. Backend замінено програмною I²C, функціональний драйвер лишився той самий.
