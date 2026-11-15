#!/usr/bin/env bash
# Cross-compiles the avocadOS shell to a 32-bit Windows exe.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
i686-w64-mingw32-gcc -Os -s -mwindows -static-libgcc -D_WIN32_WINNT=0x0501 -Wall \
    -o build/avocado-shell.exe shell/avocado-shell.c -luser32 -lgdi32 -lshell32
file build/avocado-shell.exe
