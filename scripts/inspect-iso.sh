#!/usr/bin/env bash
# Prints how a ReactOS install disc is laid out and how its setup declares files, for debugging.
set -euo pipefail
ISO=$1
tmp=$(mktemp -d)
x() { xorriso -osirrox on -indev "$ISO" -extract "$1" "$2" >/dev/null 2>&1; }

echo "== files on disc"
xorriso -indev "$ISO" -find / -maxdepth 2 -exec lsdl 2>/dev/null | cut -c1-140 | head -n 40

x /reactos/reactos.cab "$tmp/reactos.cab"
x /reactos/reactos.inf "$tmp/reactos.inf"
x /reactos/txtsetup.sif "$tmp/txtsetup.sif"
x /reactos/unattend.inf "$tmp/unattend.inf"

echo "== cab: summary"
cabextract -l "$tmp/reactos.cab" | head -n 12
echo "== cab: number of files"
cabextract -l "$tmp/reactos.cab" | grep -c '|' || true
echo "== cab: a sample of file names with sizes"
cabextract -l "$tmp/reactos.cab" | sed -n '4,30p'
echo "== cab: any 'Program Files' or 'programs' dirs"
cabextract -l "$tmp/reactos.cab" | grep -i -E 'program|python|firefox|gecko' | head -n 10 || true

echo "== reactos.inf: encoding and size"
file "$tmp/reactos.inf" || true
head -c 4 "$tmp/reactos.inf" | od -An -tx1 || true
cp "$tmp/reactos.inf" "$tmp/reactos.u8"
if [ "$(head -c 2 "$tmp/reactos.inf" | od -An -tx1 | tr -d ' ')" = "fffe" ]; then
    iconv -f UTF-16 -t UTF-8 "$tmp/reactos.inf" > "$tmp/reactos.u8" || true
fi
tr -d '\r' < "$tmp/reactos.u8" > "$tmp/reactos.txt"
wc -l "$tmp/reactos.txt" || true
echo "== BEGIN reactos.inf"
cat "$tmp/reactos.txt" | cut -c1-200
echo "== END reactos.inf"
echo "== txtsetup.sif lines mentioning cab or inf"
tr -d '\r' < "$tmp/txtsetup.sif" | grep -n -i -E 'cab|\.inf|SourceDisks|Files' | cut -c1-200 | head -n 30 || true
echo "== END"
