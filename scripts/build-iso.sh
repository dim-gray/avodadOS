#!/usr/bin/env bash
# Remasters the ReactOS install ISO: adds the avocadOS payload under \avocados on the disc.
# usage: scripts/build-iso.sh [output.iso]
# Set AVOCADOS_UNATTEND=1 to also add the unattended-install file (CI test builds only).
set -euo pipefail
cd "$(dirname "$0")/.."

ISO_IN=$(ls cache/reactos/*.iso | head -n1)
OUT=${1:-dist/avocadOS.iso}
P=build/payload

rm -rf "$P"
mkdir -p "$P/avocados/apps" "$P/avocados/avocadOS/config" "$P/avocados/avocadOS/wallpapers" "$(dirname "$OUT")"

# Windows batch files need CRLF line endings.
sed 's/\r$//; s/$/\r/' kit/first-boot.cmd > "$P/avocados/first-boot.cmd"
cp build/avocado-shell.exe "$P/avocados/avocadOS/"
cp kit/apps.ini "$P/avocados/avocadOS/config/"
cp kit/devtools.html "$P/avocados/avocadOS/"
cp build/wallpapers/*.bmp "$P/avocados/avocadOS/wallpapers/"
cp cache/firefox.exe cache/vlc.exe cache/libreoffice.msi cache/python.msi "$P/avocados/apps/"

ARGS=(-indev "$ISO_IN" -outdev "$OUT" -boot_image any replay -joliet on -map "$P/avocados" /avocados)
if [ "${AVOCADOS_UNATTEND:-0}" = "1" ]; then
    ARGS+=(-map kit/unattend-ci.inf /reactos/unattend.inf)
fi

rm -f "$OUT"
xorriso "${ARGS[@]}" -commit
ls -lh "$OUT"
