# Мета й отриманий результат

Мета - з’єднати окремий цифровий термометр середовища, аналоговий термометр зразка та контакт кришки в одній HAL-прошивці BatchTherm і перевірити значення, сигнали та помилки незалежними способами. Секторний SD bring-up додано для перевірки придатності реального сховища наступного етапу.

Середовище: Ubuntu 24.04, Docker, STM32 HAL, Wokwi; Nucleo-C031C6, форма B. Контейнер виконує ARM build і host tests; Wokwi працював з macOS. Фізичної плати, вимірів струму та осцилографічних фронтів немає.

У спільному запуску BMP180 дає 24.0 °C, NTC - 60.0 °C, контакт перемикає OPEN/CLOSED із debounce; PA5 показує спільну валідність і входження в коридор 58–62 °C. SDHC реально записує та читає 512-байтовий сектор. Це сенсорна підсистема ЛР2; автомат RAMP/SOAK/COOL, журнал партій і FreeRTOS ще не реалізовані.

# Розмежування шарів

![Component diagram: власні модулі та межа HAL](docs/figures/components.png)

Core містить startup, clock, SysTick та USART2 IRQ. Platform відповідає за піни, ADC, UART, GPIO I²C/SPI та bring-up. Devices отримує callbacks шини й не знає HAL. App реалізує медіану, debounce, freshness і коридор. tools/check_layers.py автоматично відхиляє HAL/STM32/board.h в App/Devices.

[Реалізація підсистеми](https://github.com/danyliuk361/BatchTherm/commit/f8c67c0); [host тести, власні вектори і CI](https://github.com/danyliuk361/BatchTherm/commit/6b208f3). Докладна структура - docs/architecture.md. Сторонні файли ST vendored, версія та SHA-256 збережені; власний код зібрано з -Wall -Wextra без попереджень.

---
# Підключення і обґрунтування

| MCU | Сигнал / режим | Пристрій / рівні |
|---|---|---|
| PA0, LQFP11 | ADC_IN0, CN8.1 A0 | NTC OUT 0–3.3 V; fixed10k до3V3, NTC доGND |
| PB0, LQFP19 | input pull-up, D10 | контакт доGND; натиснута кнопка=CLOSED |
| PB8/PB9, LQFP47/48 | software I²C, GPIO OD LOW / input release | BMP180 SCL/SDA, pull-up4.7k до3V3; AF6 доступна, але не обрана |
| PA2/PA3, LQFP13/14 | USART2 AF1, 1152008N1 | монітор UART, 3.3-V логіка |
| PA8/PA9 | GPIO output | ADC_MARK / LID_DB для аналізатора |
| PA5 | GPIO output | штатний LD4; не використовується як SPI SCK |
| PB1/PB3/PB4/PB5 | GPIO CS/SCK/MISO/MOSI, SPI mode0 | SDHC, 3.3 V, CS LOW тільки в транзакції |

Точні роз’єми, схема і порядок монтажу - docs/pinout.md та diagram.json. Підстава: DS13867 табл.12–15 для кристала, RM0490 для ADC/RCC/USART, UM2953 табл.11 і рис.7 для плати. Номери LQFP не є номерами morpho. У UM2953 є суперечливі підписи PB8/PB9; функції звірено за таблицею AF та Arduino-таблицею. У Wokwi використано імена MCU.

BMP180 має адресу 0x77, D0=0x55, calibration AA–BF, температура запускається F4=0x2E і читається F6/F7. NTC та контакт ідентифікаційних регістрів не мають. SDHC визначається CMD8 echo0x1AA та OCR.CCS=1, а не JEDEC ID NOR-flash.

Відхилено SPI PA5/6/7 через конфлікт PA5 із LED; CS PB0 через контакт кришки; заміну NTC другим цифровим термометром, яка прибрала б ADC. SWD PA13/14 збережені; DMA не застосовується.

Консервативний peak входів без плати/LED/SD: BMP1801 mA, NTC≤0.33 mA, контакт≤0.33 mA, pull-up шин<1.6 mA; разом<3.3 mA. UM2953 §6.5.2 задає сумарне обмеження плати і shield500 mA після USB enumeration; точний запас невідомий без Iплати. Для фізичної SD потрібне окреме стабільне3.3-V джерело з урахуванням peak конкретної карти. Wokwi не перевіряє цей бюджет.

SYSCLK48 MHz, APB divider1, ADC=PCLK/4=12 MHz, sampling79.5 циклів. I²C/SPI програмні, тому периферійного prescaler немає; фактичні clocks виміряно нижче. .ioc актуалізований вручну, генерація CubeMX у цьому сеансі не виконувалася.

---
# Спільна робота і self-test

![Реальний запуск BatchTherm: входи та сигнал валідності](docs/metrics/lab2/lid-final/lid-closed.png)

![UART того самого запуску: CLOSED, usable=1, band=1](docs/figures/lab2-uart-valid.png)

Стартовий машинний формат: BOOT product=BatchTherm lab=2 units=dC; TEST device=BMP180 addr=77 id=55 result=OK; TEST device=NTC adc=809 sample_dC=600 result=OK; TEST device=LID; TEST device=SD result=0 ready=1 ocr=C0FF8000. Нуль для SD - BT_OK, не значення температури.

FRAME містить MCU t у мс, raw ADC, sample_dC, median_dC, valid кожного каналу, ambient_dC, UT, контакт, usable і band. Температури в десятих °C: 600 означає60.0 °C. Значення з valid=0 використовувати заборонено; числове поле тоді може зберігати останнє вдале вимірювання.

Команди ЛР2: selftest/capture повторюють ID/calibration/ADC; status/counts показують стан; nack/recover задають відсутню/правильну I²C адресу; sdwrite/sdread перевіряють scratch-сектор32. UART IRQ заповнює кільце128 байт; foreground розбирає рядок≤79 байт, надмірний рядок відкидається до LF. ISR не викликає printf. Це не продуктовий CLI profile/start/abort/export майбутньої ЛР3.

---
# I²C: декодування і перевірка даних

![PulseView: власний VCD, адреса77, регістрD0, ID55 та calibration AA](docs/figures/pulseview-i2c.png)

Захват docs/captures/lab2/gpio-adc-i2c-uart.vcd, trigger off, D0=SCL і D1=SDA. Event-driven шкала1 ns; імпорт sigrok/PulseView downsample100 дає10 MHz. Цифрових відліків достатньо для цих повільних шин, але точність RC фронтів чи напруги не досліджена.

Транзакція ID: START→EE ACK→D0 ACK→repeated START→EF ACK→55 NACK→STOP. EE/EF - адреса77 зі write/read bit. Останній NACK формує master штатно. ID-читання10.453350–23.357325 мс, тривалість12.903975 мс.

Calibration burst AA–BF: 01 98 FF B8 C7 D1 7F E5 7F F5 5A 71 18 2E 00 04 80 00 DD F9 0B 34. З нього AC5=32757, AC6=23153, MC=-8711, MD=2868. Після F4=2E пауза до наступного START5.419875 мс, більше datasheet4.5 мс. UT7164h=29028 дає24.0 °C, як у UART. Захоплені байти використано як host vectors; окремий приклад Bosch UT27898 дає15.0 °C.

| Ручний розбір SDA на rising SCL | Значення | Дев’ятий такт |
|---|---|---|
| 11101110 | EE → адреса77, write | SDA0, ACK |
| 11010000 | D0, регістр ID | SDA0, ACK |
| 11101111 | EF → адреса77, read | SDA0, ACK |
| 01010101 | 55, очікуваний ID | SDA1, master NACK |

Для EE rising SCL10.798850,11.146825,11.492325,11.841075,12.189050,12.534550,12.880050,13.231275 мс; ACK13.576025 мс. Це аналіз рівнів, незалежний від decoder annotations. Середній SCL period312.872 μs, min235.500, max356.750 μs у1192 міжфронтових інтервалах: приблизно2.8–4.2 kHz. Шина повільна через HAL GPIO direction/overhead;100 kHz тут є upper limit режиму, а не отриманою швидкістю.

---
# Аномалія I²C та сховище SPI

Команда nack свідомо обирає адресу76. На шині EC із SDA HIGH у дев’ятому такті; UART BUS t=1648 addr=76 status=NACK. Канал середовища невалідний. recover повертає77, повторно читає ID/calibration і дає BUS t=2642 status=OK. Сировина інтеграційного запуску збережена, а не замінена очікуваним логом.

Початковий HAL_I2C_Mem_Read мав ACK адреси/регістрового байта без завершення. Спроба HAL Master TX/RX показала55 на шині, але наступні CR2/ISR і читання не узгоджувалися з STOP. У фіналі замінено лише backend на software I²C через HAL GPIO: HIGH=input release, LOW=open-drain. Драйвер BMP180 не змінює стандартний протокол. Це спостережена несумісність конкретної комбінації HAL/Wokwi; причина не приписується фізичному сенсору. Невдалі спроби збережені в docs/metrics/lab2.

# SPI SDHC: реальні запис і читання

У diagram.storage.json аналізатор під’єднано до SCK/MOSI/MISO/CS, UART і ADC marker. Це той самий бінарник і ті самі пристрої, змінені тільки канали запису. Mode0, MSB first, CS LOW увесь command/data. SCK period95.525–98.500 μs, mean96.033 μs, близько10.4 kHz, менше400-kHz межі ініціалізації.

| Команда | MOSI / відповідь MISO |
|---|---|
| CMD0 | 40 00000000 95 →01 |
| CMD8 | 48 000001AA 87 →01 /000001AA |
| CMD55 + ACMD41 | HCS40000000 → ready00 |
| CMD58 | OCR C0FF8000, CCS=1: SDHC block addressing |
| CMD24, sector32 | FE+512 bytes+CRC dummy, accepted05, busy доFF |
| CMD17, sector32 | FE+512 bytes+CRC, байти збігаються |

CMD24 CS interval420.636 мс; CMD17 407.868 мс. Сам512-byte payload при96 μs/bit займає≈393.2 мс; різницю дають command, token polling, CRC та виклики GPIO/ISR. UART WRITE_READ833 мс близький до суми двох операцій; READ_ONLY410–411 мс близький до одного CS interval. З VCD незалежно виділено payload, він дорівнює pattern(n*37+19 mod256) в обох напрямах.

Після export образ містить ці512 байтів у sector32. У новому процесі Wokwi завантажено збережений образ, виконано тільки sdread: result0, mismatch0. Це збереження на рівні образу моделі, не RAM і не штучний delay. Atomicity, CRC журналу та зрив фізичного живлення належать ЛР3. SPI-NOR WREN/JEDEC не застосовуються: у продукті SDHC, її command order перевіряють окремі host-тести.

---
# GPIO, ADC та фізичні одиниці

![PulseView: контакт, debounce і маркери опитування ADC](docs/figures/pulseview-gpio.png)

NTC використовує B=3977 K, R25=10 kΩ, fixed10 kΩ до3V3. Rntc/R25=ADC/(4095−ADC); T=1/(ln(Rntc/R25)/B+1/298.15)−273.15. ADC12 bit,0–4095. Raw≤8 або≥4087 одразу невалідний; продуктовий діапазон0–85 °C. Медіана3 температур прибирає одиночний імпульс; після відмови вікно очищується, valid відновлюється лише після3 вдалих вибірок. Абсолютна точність фізичного NTC без калібрування не доведена.

| Задано у Wokwi | Raw ADC / результат | Вердикт |
|---|---|---|
| 25 °C | 2048 /250dC | поза коридором |
| 58 °C | 857 /580dC | включна нижня межа |
| 62 °C | 764 /620dC | включна верхня межа |
| 63 °C | 742 /630dC | band0 |
| −5 °C | 3343 /sample_valid0 | поза продуктовим діапазоном |
| Повернення60 °C | 809, спершу valid0, після3 вибірок600/valid1 | відновлення без старого фільтра |

Контакт опитується з ціллю5 мс, debounce25 мс однакового рівня. На VCD є bounce; від останнього raw переходу до LID_DB: CLOSED59.668 мс, OPEN59.707 мс. UART MCU t=1044/2244 мс узгоджується з GPIO1045.366/2245.563 мс з урахуванням точки мітки. Тут2 події, а не30-event latency дослідження ЛР3.

ADC не має цифрового протоколу. PA8 обрамлює HAL Start/Poll/Read/Stop:30 вікон1233.975–1234.500 μs,29 інтервалів99.981–200.120 мс; максимальний включає startup паузу ініціалізації SD. Розрахунок92 ADC cycles/12 MHz=7.67 μs стосується самого перетворення. Вікно GPIO включає ПЗ й поведінку моделі; за ним не можна визначити фізичну conversion latency. Під час SD bring-up є довгі блокування; їх не оголошено виконаними deadline ЛР3.

---
# UART, host-тести і збірка

З VCD незалежно декодовано6733 UART-байти, вони побайтно рівні сирому UART integration-final/uart.log, включно з CR/LF. Номінал1152008N1, VCD bit≈8.69 μs. Наприкінці counts: adc88, bmp46, errors5 від свідомого NACK, commands6, rx_overflow0, rx_errors0. Це скінченний сценарій, не гарантія безмежного потоку команд.

Тести запускаються make test IMAGE=batchtherm-build:lab2 у Docker окремо від ARM firmware. Мінімальний assert-based runner не має зручних fixture/filter/report можливостей Unity, але перевіряє bus call order та ін’єкцію помилок і падає при першій невідповідності. GCC coverage збирається окремо від target. Поріг≥90% executable lines на кожен Devices/App файл обрано для цього обсягу, з обов’язковими error і boundary cases; line coverage не замінює аналіз відмов.

| Модуль | Виконані рядки | Line coverage | Основні випадки |
|---|---|---|---|
| BMP180 | 43/44 | 97.73% | Bosch15.0 °C, власний UT24.0 °C, ID/calibration, NACK/timeout/IO, command order |
| NTC | 10/10 | 100% | незалежна модель5…80 °C, власний ADC809, rails і позадіапазонні дані |
| App quality | 40/40 | 100% | median spike/warm-up/reset, inclusive58/62, freshness600/601 мс, debounce і wrap |
| SDHC | 91/101 | 90.10% | init order/CRC/echo/OCR,512-byte readback, bad token/response, busy timeout |

Branch directions покриті не повністю: BMP18080.77%, NTC90.00%, App91.18%, SD82.76%. Platform, переривання, електрика та модель не включені до цих host-відсотків; їх перевіряє окремий Wokwi run. Випадки невдалого тесту й першої збірки збережено, остаточний make test пройшов.

Чиста ARM build: text27496, data116, bss3004 байти. Flash payload27612 з32768, статичні data+bss3120 з12288; це не high-water mark стека. ELF SHA25644b89de4d5a40ba93abb7a817fedf1609814882259301a014d8ed1e0ab5f0b58. Виміряні ELF/HEX/BIN збережено в docs/evidence/lab2. CI workflow запускає make build і make test на PR; виконання remote CI поки не підтверджене. Toolchain image та версії пакетів записані в docs/toolchain-lock.txt.

![Реальний Terminal: автоматична звірка збережених доказів](docs/figures/lab2-verification.png)

---
# Фактичні проблеми і виправлення

| Симптом | Root cause | Мінімальна корекція й доказ |
|---|---|---|
| ADC типи/функції не збирались | HAL ADC module був закоментований, у шаблоні бракувало ADC files | Додано офіційні ST1.0.1 ADC/ADCEx/LL, увімкнено модуль; ARM build пройшла |
| Код ADC змінювався за сталої температури | Для configurable sequencer використано rank fixed sequence | ADC_REGULAR_RANK_1; стала60 °C дає стабільний809 |
| 60 °C перетворювалося на невалідну температуру | Формула відповідала протилежній полярності дільника | ADC/(4095−ADC), уточнено схему; тест попередньої версії впав, виправлена і VCD-вектор пройшли |
| ACK на I²C був, завершення/наступне читання ламалось | Спостережена несумісність hardware HAL/Wokwi, stale CR2/ISR | Ізольований GPIO backend; ID55,22-byte calibration і UT підтверджені VCD |
| Software release давав timeout | Output-mode feedback не відображав очікуваний зовнішній рівень в цій моделі | HIGH=input release+pull-up; дані/ACK читаються через input; driver незмінний |
| Кілька bring-up команд стали UNKNOWN | Polling RX губив символи під час blocking UART/SD | IRQ ring128, counters;6 команд виконані, overflow0/RXerrors0 |
| Host gcc не знаходив stdint.h | Мінімальний Docker образ не мав libc6-dev | Додано host headers; make test/coverage пройшли |

Корекції реалізації - [f8c67c0](https://github.com/danyliuk361/BatchTherm/commit/f8c67c0), перевірки та CI - [6b208f3](https://github.com/danyliuk361/BatchTherm/commit/6b208f3). Звичайний build не використовує фальшиві sensor values; тестові адреси і scratch pattern явно відокремлені від вимірів.

# Висновок і межі

Сенсорна підсистема працює спільно й пояснює свої значення та валідність. Незалежні декодовані UART/I²C/SPI та GPIO підтверджують дані, протокол, debounce і факт секторного збереження; host-тести перевіряють фізичні одиниці та відмови. Найпомітніша ціна поточного backend - зайнятість CPU і довгі SD операції. Їх слід дослідити як обмеження super loop, не приховувати оптимізацією без вимоги.

ЛР3 має реалізувати сам температурний автомат, реальний журнал з CRC/recovery і виміряти S1/S2/S3, включно з неперервною годиною MCU часу. Такої baseline зараз немає. ЛР4 має зберегти продукт/CLI та порівняти обидві версії за однакових умов в один день. Ця робота не підтверджує фізичну точність NTC/BMP180, RC фронти, струм, SD endurance або power-loss atomicity.

# Джерела

[ST DS13867](https://www.st.com/resource/en/datasheet/stm32c031f4.pdf), табл.12–15; [RM0490](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), RCC/ADC/USART; [UM2953](https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf), §6.5.2, рис.7 і табл.11; [Bosch BMP180](https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf), с.6–7,11–15,18–23; [Vishay doc29049](https://www.vishay.com/docs/29049/ntcle100.pdf), с.1–4; [SD Association simplified specifications](https://www.sdcard.org/downloads/pls/), SPI mode; [Wokwi NTC](https://docs.wokwi.com/parts/wokwi-ntc-temperature-sensor), [BMP180](https://docs.wokwi.com/parts/board-bmp180), [microSD](https://docs.wokwi.com/parts/wokwi-microsd-card), [logic analyzer](https://docs.wokwi.com/parts/wokwi-logic-analyzer); [ST HAL1.0.1](https://github.com/STMicroelectronics/stm32c0xx-hal-driver/tree/v1.0.1). Повний розбір, параметри й raw paths - docs/captures/analysis.md.
