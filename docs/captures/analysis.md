# Верифікація сигналів BatchTherm, ЛР2

Власні записи Wokwi від06.11.2026; фінальний ELF SHA256 44b89de4d5a40ba93abb7a817fedf1609814882259301a014d8ed1e0ab5f0b58. Сирі VCD отримані від компонента wokwi-logic-analyzer. Нічого не синтезовано з UART чи очікуваного профілю.

## Записи і відтворення

| Файл | Канали D0…D7 | Джерело / тригер |
|---|---|---|
| lab2/gpio-adc-i2c-uart.vcd | SCL,SDA,UART_TX,ADC_MARK,LID_RAW,LID_DB,LED,UART_RX | diagram.json; boot/selftest, утримання й відпускання контакту; trigger off |
| lab2/spi-sd-i2c-uart.vcd | SCK,MOSI,MISO,CS,UART_TX,ADC_MARK,SCL,SDA | diagram.storage.json; capture, nack, recover, sdwrite, sdread, counts; trigger off |

Часова шкала VCD1 ns - дискретність timestamps, не частота фізичної оцифровки. Wokwi записує зміни рівня; buffer1 000 000 edge entries, приблизно9 MB host memory за документацією моделі. Усі збережені записи нижче місткості. Для sigrok/PulseView імпорт downsample=100 означає10 MHz, крок0.1 μs: приблизно87 відліків на UART-біт і тисячі на повільний GPIO I²C/SPI. Ідеальні фронти не підтверджують RC rise time, ringing або рівні живлення. Природний hardware trigger для I²C - START, SPI - CS falling; у збереженому boot захваті trigger off гарантує, що початкова ID/calibration не втрачена. На фізичному стенді потрібні common GND, перевірка рівнів та осцилограф.

## I²C: адреса, ID, burst та конверсія

Незалежний sigrok export: lab2/i2c-decoded.txt. Події без decoder зчитує tools/analyze_vcd.py; перед аналізом він групує зміни одного timestamp, щоб не трактувати zero-duration ordering artifacts як START.

1. START→EE ACK→D0 ACK→repeated START→EF ACK→55 NACK→STOP. 7-bit адреса77; EE=(77<<1)|0, EF=(77<<1)|1. Відсутність ACK після останнього55 є штатним завершенням читання master, не помилкою сенсора.
2. AA burst читає22 байти: `01 98 FF B8 C7 D1 7F E5 7F F5 5A 71 18 2E 00 04 80 00 DD F9 0B 34`. Це calibration map AA–BF із Bosch datasheet с.13–15,18. AC5=32757, AC6=23153, MC=-8711, MD=2868.
3. Запис F4=2E, пауза від STOP119.768575 мс до наступного START125.188450 мс =5.419875 мс, більше4.5 мс. F6 burst=`71 64`→UT29028; власний compensation дає240 десятих °C, як у UART FRAME. Ці захоплені байти стали tests/capture_vectors.h. Окремий datasheet test UT27898 дає15.0 °C.

ID transaction триває12.903975 мс (10.453350→23.357325); calibration burst разом із address phase64.773650 мс. Середній період SCL у всіх повних байтах першого запису312.872 μs, діапазон235.500–356.750 μs (1192 міжфронтових інтервали), приблизно2.8–4.2 kHz; швидкість залежить від GPIO direction/HAL overhead і вмісту байта. У інтеграційному записі максимум періоду397.125 μs через ISR/навантаження. Це значно нижче100-kHz Standard-mode limit, а не обіцянка швидкості100 kHz. Один byte приблизно9 періодів плюс START/STOP; довга calibration узгоджується з кількістю байтів.

### Ручне декодування

Для першого EE рівні SDA на восьми rising SCL, мс:10.798850,11.146825,11.492325,11.841075,12.189050,12.534550,12.880050,13.231275. Послідовність`1 1 1 0 1 1 1 0`=0xEE, MSB first. Дев’ятий rising13.576025 мс має SDA=0: ACK. START - спад SDA при HIGH SCL10.453350 мс.

Далі D0: rising13.929525…16.369225 мс дають`1 1 0 1 0 0 0 0`, ACK16.713975. Після repeated START17.188700 читається EF=`11101111`, потім55=`01010101`. На останньому дев’ятому такті22.663350 мс SDA=1: master NACK, після нього STOP23.357325. Це розбір рівнів VCD, незалежний від sigrok annotation.

### Аномалія і відновлення

Команда nack змінює 7-bit адресу на76. На шині EC (write76) з дев’ятим SDA=1; UART BUS t=1648 addr=76 status=NACK. Дані середовища стають невалідними. recover повертає77: BUS t=2642 status=OK, повторний ID/calibration і вимір. Згенеровано свідомо, без підміни результату HAL/driver. Початкові невдалі hardware-I2C записи збережено окремо; пояснення backend у architecture.md.

## SPI SDHC

lab2/spi-decoded.txt - sigrok MOSI/MISO export; lab2/spi-frames.json - byte frames, межі CS і clocks. Mode0: clock idle LOW, sampling rising, MSB first, CS LOW увесь command/data. SCK period95.525–98.500 μs, mean96.033 μs, приблизно10.4 kHz; нижче400-kHz initialization limit. Це програмна SPI, без hardware prescaler.

Послідовність команд: CMD0 `40 00000000 95`→01; CMD8 `48 000001AA 87`→01/000001AA; CMD55→01, ACMD41 HCS→00; CMD58→00/C0FF8000, CCS=1. Драйвер свідомо підтримує SDHC block addressing, не SDSC.

CMD24 sector32 (`58 00000020 01`) передає FE+512 bytes+2 CRC dummy, data response accepted05 і busy доFF. Повний CS interval420.636 мс. CMD17 (`51 00000020 01`) повертає FE+512 bytes+CRC, CS interval407.868 мс. За8*512*96 μs сам payload≈393.2 мс; залишок - command, polling, CRC, GPIO/ISR. Незалежно виділені з VCD payload обох напрямів дорівнюють pattern(n*37+19 mod256). UART WRITE_READ833 мс - сума двох операцій і overhead; READ_ONLY410–411 мс близький до одного CS interval.

Образ integration-final/sd-after.img містить ті самі512 байтів у sector32. У новому процесі persistent-read-final образ минулого успішного запису завантажено до boot, виконано тільки sdread; mismatch0. Це persistent storage на рівні збереженого образу Wokwi. Падіння фізичного живлення/atomic journal ще не перевірялося.

SPI-NOR WREN/JEDEC tests непридатні, оскільки SPI-flash у цьому продукті немає. Замість них tests/test_sdcard.c перевіряє саме SD command order/CRC/echo/OCR, токени,512-byte payload і busy timeout.

## UART, GPIO, ADC

lab2/uart-decoded.txt містить6733 байти незалежного UART decode, побайтно рівні raw simulator UART integration-final/uart.log. Номінал1152008N1; VCD bit≈8.69 μs. У кінці counts: adc88, bmp46, errors5 (свідомий NACK), commands6, rx_overflow0, rx_errors0. Це обмежений сценарій, не гарантія для довільного потоку.

На контакті реально є bounce edges. Від останнього переходу raw до LID_DB: CLOSED59.668 мс, OPEN59.707 мс. UART фіксує ті самі зміни в MCU мс1044/2244, GPIO -1045.366/2245.563 мс; різниця включає положення мітки/виклик HAL. Debounce мінімум25 мс; спостереження пізніше через блокувальні операції foreground. Це2 події ЛР2, не30-event latency дослідження ЛР3.

ADC не має цифрової шини. PA8 HIGH обрамлює HAL_ADC_Start/Poll/Read/Stop:30 вікон1233.975–1234.500 μs.29 інтервалів99.981–200.120 мс; останнє включає startup паузу ініціалізації SD. Після startup ціль100 мс; у storage bring-up є довгі пропуски, які ЛР3 має обробити за PRD. Теоретичні92 ADC cycles/12 MHz=7.67 μs стосуються самого перетворення, тоді як маркер включає програмні витрати й поведінку моделі; фізичну conversion latency за цією рамкою визначити не можна.

Функціональні входи: sample25→ADC2048→250dC,58→857→580,62→764→620,63→742→630. Межі58/62 in-band,63 out-of-band. При-5 °C ADC3343, sample_valid0; старе числове поле не використовується без valid. Після повернення60 °C кадр спершу невалідний, потім після3 вибірок median600/valid1. Окремі host-тести перевіряють rails0/4095, одиночний spike та wrap MCU ms.

## Межі висновків

Симулятор доводить виконання драйверів і цифровий протокол, але не точність термометра, електричний бюджет, I²C RC фронти, витривалість SD, power-loss recovery чи час фізичної flash. Host tests, ARM build і Wokwi run - окремі рівні доказу. S1/S2/S3 продукту не виконувалися; baseline ЛР3 не виміряна.
