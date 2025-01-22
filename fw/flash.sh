#!/bin/bash
#cmake -B build -S .
#touch src/main.cpp
cmake --build build -j4

python python/io.py set system.enter_bootloader
sleep 5
udisksctl mount -b /dev/disk/by-label/RPI-RP2
cp build/goat.uf2 /run/media/$USER/RPI-RP2
sync
