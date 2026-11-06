#!/bin/sh
set -eu
python3 tools/check_layers.py
mkdir -p tests/build
cd tests/build
rm -f ./*.gcda ./*.gcno
gcc --coverage -std=c11 -Wall -Wextra -Werror -g -O0 -I../../firmware/Devices -I../../firmware/App ../test_thermal.c ../test_sdcard.c ../../firmware/Devices/sdcard.c ../../firmware/Devices/bmp180.c ../../firmware/Devices/ntc.c ../../firmware/App/quality.c -lm -o thermal
./thermal
gcov -b -c thermal-*.gcno

cd ../..
python3 tools/coverage.py
gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/App -Ifirmware/Devices tests/test_batch.c firmware/App/batch.c firmware/Devices/sd_stream.c -o tests/build/batch
./tests/build/batch
gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/Platform tests/test_timer_extend.c -o tests/build/timer_extend
./tests/build/timer_extend
