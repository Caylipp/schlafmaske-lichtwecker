#!/bin/sh
# Host-Unit-Tests der reinen Logik (lichtwecker/lichtwecker_logic.h). Keine Hardware, kein Arduino nötig.
set -e
cd "$(dirname "$0")"
CXX="${CXX:-clang++}"
mkdir -p build
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 test_logic.cpp -o build/test_logic
./build/test_logic "$@"
