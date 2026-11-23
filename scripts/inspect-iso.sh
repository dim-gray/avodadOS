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
file "$tmp/reactos.inf"; wc -c "$tmp/reactos.inf"
iconv -f UTF-16 -t UTF-8 "$tmp/reactos.inf" 2>/dev/null > "$tmp/reactos.u8" || cp "$tmp/reactos.inf" "$tmp/reactos.u8"
echo "== reactos.inf: section headers"
grep -n '^\[' "$tmp/reactos.u8" | head -n 60
echo "== reactos.inf: first 60 lines"
head -n 60 "$tmp/reactos.u8" | tr -d '\r'
echo "== reactos.inf: Directories section (first 40)"
awk '/^\[Directories\]/{f=1;next} /^\[/{f=0} f' "$tmp/reactos.u8" | tr -d '\r' | head -n 40
echo "== reactos.inf: SourceFileNames sample (first 15 and 15 around notepad)"
awk '/^\[SourceFileNames/{f=1;print;next} /^\[/{f=0} f' "$tmp/reactos.u8" | tr -d '\r' | head -n 15
grep -n -i 'notepad' "$tmp/reactos.u8" | tr -d '\r' | head -n 8
echo "== reactos.inf: SourceDisksNames"
awk '/^\[SourceDisksNames/{f=1;print;next} /^\[/{f=0} f' "$tmp/reactos.u8" | tr -d '\r' | head -n 10
echo "== reactos.inf: DestinationDirs"
awk '/^\[DestinationDirs\]/{f=1;print;next} /^\[/{f=0} f' "$tmp/reactos.u8" | tr -d '\r' | head -n 12
echo "== txtsetup.sif: head and file-related lines"
head -n 40 "$tmp/txtsetup.sif" | tr -d '\r'
grep -n -i -E 'cab|reactos.inf|SourceDisksNames' "$tmp/txtsetup.sif" | tr -d '\r' | head -n 12
