#!/bin/sh
# Build and run the whole-firmware simulation (no hardware needed).
# Needs g++ and gcc, and the U8g2 library that PlatformIO downloads, so run
# "pio run" once first. Linux, macOS or WSL (uses fork).
# Usage, from the project folder:   sh native_tests/run_sim.sh [scenario_name]
# Runs every scenario three times: as shipped (POWER_SAVE on, screen always
# on), with the dark screen and peeks switched on, and with POWER_SAVE off.
# Set SIM_ECHO=1 to see the firmware's serial output while it runs.
set -e
cd "$(dirname "$0")/.."
U8G2=.pio/libdeps/duckbox/U8g2/src
if [ ! -d "$U8G2" ]; then
  echo "U8g2 not found in $U8G2 - run 'pio run' once so PlatformIO downloads it." >&2
  exit 2
fi
mkdir -p .native_build/u8g2

# U8g2's C sources (fonts included) are compiled once and kept.
if [ ! -f .native_build/libu8g2.a ]; then
  echo "compiling U8g2 for the PC (first run only)..."
  for f in "$U8G2"/clib/*.c; do
    gcc -O1 -w -c "$f" -I"$U8G2/clib" -o ".native_build/u8g2/$(basename "$f" .c).o"
  done
  ar rcs .native_build/libu8g2.a .native_build/u8g2/*.o
fi

status=0
for mode in shipped peeks off; do
  case $mode in
    shipped) defs="-DDUCK_POWER_SAVE=1" ;;
    peeks)   defs="-DDUCK_POWER_SAVE=1 -DDUCK_SCREEN_PEEK_EVERY_MS=20000" ;;
    off)     defs="-DDUCK_POWER_SAVE=0" ;;
  esac
  echo "== $mode: $defs =="
  g++ -std=c++17 -Wall -Wextra -O1 -g $defs \
      -Inative_tests/sim/stubs -Inative_tests/sim -Isrc -Iinclude -I"$U8G2" \
      native_tests/sim/sim_main.cpp native_tests/sim/sim_board.cpp \
      src/*.cpp "$U8G2/U8g2lib.cpp" "$U8G2/U8x8lib.cpp" \
      .native_build/libu8g2.a -o .native_build/duck_sim_$mode
  ./.native_build/duck_sim_$mode "$@" || status=1
done
exit $status
