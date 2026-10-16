# BatchTherm

Данилюк Вадим Ігорович, ІО-36. Реєстратор температурного циклу партії: нагрівання, неперервна витримка, охолодження; відкриття кришки перериває валідність. Нагрівачем не керує.

Середовище: Ubuntu 24.04, Docker, STM32 HAL, Wokwi. Nucleo-C031C6, форма B.

`make build IMAGE=batchtherm-build:lab1` створює firmware/build/firmware.elf, .hex, .bin; `make clean` видаляє тільки build. Для запуску відкрийте корінь у VS Code і виконайте Wokwi: Start Simulator; UART монітор вбудовано у Wokwi. PA5 LED змінює стан раз на секунду. Trial не потрібен і не вмикається.

* [PRD і критерії](docs/PRD.md)
* [Компоненти та джерела](docs/hardware-components.md)
* [План етапів](docs/lab-plan.md)
* [Дозволена стартова заготовка](docs/starter-template.md)

Гілка lab1 містить bring-up UART/LED, дослідження і вимоги. Драйвери та спільний стенд належать lab2. Фінальна готовність визначається доказами і переліком залишку в локальному GUIDE.
