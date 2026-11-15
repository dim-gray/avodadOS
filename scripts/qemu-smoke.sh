#!/usr/bin/env bash
# Boots an ISO in QEMU headless and saves screenshots.
# usage: scripts/qemu-smoke.sh ISO OUTDIR MINUTES [boot|install]
#   boot     just boot the disc and watch it
#   install  also attach a blank 4 GB disk (for the unattended-install test ISO)
# Exits non-zero if every screenshot is blank, which means nothing booted.
set -euo pipefail
ISO=$1
OUT=$2
MIN=${3:-3}
MODE=${4:-boot}
MON=/tmp/avocados-qemu-mon.sock

mkdir -p "$OUT"
rm -f "$MON"

ARGS=(-m 768 -accel kvm -accel tcg -cpu qemu32 -vga std
      -netdev user,id=n0 -device rtl8139,netdev=n0
      -display none -monitor "unix:$MON,server,nowait" -serial none)
if [ "$MODE" = "install" ]; then
    qemu-img create -f qcow2 "$OUT/disk.qcow2" 4G >/dev/null
    ARGS+=(-drive "file=$OUT/disk.qcow2,format=qcow2,if=ide,index=0" -boot order=c,once=d)
fi
ARGS+=(-cdrom "$ISO")

qemu-system-i386 "${ARGS[@]}" &
QPID=$!
trap 'kill $QPID 2>/dev/null || true' EXIT

for _ in $(seq 1 30); do [ -S "$MON" ] && break; sleep 1; done

shot() { # name
    echo "screendump $OUT/$1.ppm" | socat - "UNIX-CONNECT:$MON" >/dev/null
    sleep 1
    convert "$OUT/$1.ppm" "$OUT/$1.png"
    rm -f "$OUT/$1.ppm"
}

alive=0
for i in $(seq 1 $((MIN * 2))); do
    sleep 30
    name=$(printf 't%04ds' $((i * 30)))
    shot "$name"
    sd=$(convert "$OUT/$name.png" -format '%[fx:standard_deviation]' info:)
    echo "$name: contrast $sd"
    # Print what is on screen as text (OCR), but only when the picture changed, so CI logs show progress.
    sum=$(md5sum < "$OUT/$name.png" | cut -d' ' -f1)
    if [ "$sum" != "${last_sum:-}" ] && command -v tesseract >/dev/null 2>&1; then
        echo "--- screen text at $name"
        convert "$OUT/$name.png" -resize 200% -colorspace Gray "$OUT/ocr.png"
        tesseract "$OUT/ocr.png" stdout 2>/dev/null | grep -v '^[[:space:]]*$' | head -n 40 || true
        echo "---"
    fi
    last_sum=$sum
    if awk "BEGIN{exit !($sd > 0.02)}"; then alive=1; fi
done

[ "$alive" = "1" ] || { echo "every screenshot was blank: the ISO did not boot" >&2; exit 1; }
echo "ISO produced a live screen"
