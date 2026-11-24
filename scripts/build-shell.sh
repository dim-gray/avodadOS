#!/usr/bin/env bash
# Cross-compiles the avocados shell to a 32-bit Windows exe.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
i686-w64-mingw32-gcc -Os -s -mwindows -static-libgcc -D_WIN32_WINNT=0x0501 -Wall -Wextra \
    -Wno-unused-parameter -Wno-cast-function-type \
    -o build/avocados-shell.exe shell/avocados-shell.c -luser32 -lgdi32 -lshell32 -ladvapi32
file build/avocados-shell.exe
