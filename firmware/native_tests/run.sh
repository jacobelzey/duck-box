#!/bin/sh
# Build and run the native unit tests (no hardware needed). Needs g++.
# Usage, from the project folder:   sh native_tests/run.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p .native_build
g++ -std=c++17 -Wall -Wextra -Werror -O1 -Isrc -Iinclude \
    native_tests/test_main.cpp native_tests/test_duck_logic.cpp native_tests/test_helpers.cpp \
    native_tests/test_power_save.cpp \
    src/duck_logic.cpp src/button_decoder.cpp src/candle_flicker.cpp src/beam_sampler.cpp \
    -o .native_build/duck_tests
exec ./.native_build/duck_tests
