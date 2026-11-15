#!/usr/bin/env bash
# Prints what is in an ISO's \reactos folder, the stock unattend.inf, and registry hooks, for debugging.
set -euo pipefail
ISO=$1
tmp=$(mktemp -d)
echo "== /reactos"
xorriso -indev "$ISO" -find /reactos -maxdepth 1 2>/dev/null | head -n 60
echo "== /reactos/system32"
xorriso -indev "$ISO" -find /reactos/system32 -maxdepth 1 2>/dev/null | head -n 30
for f in unattend.inf registry.inf txtsetup.sif; do
    xorriso -osirrox on -indev "$ISO" -extract "/reactos/$f" "$tmp/$f" >/dev/null 2>&1 || { echo "== $f: missing"; continue; }
    echo "== $f: $(wc -l < "$tmp/$f") lines"
done
echo "== full stock unattend.inf"
cat "$tmp/unattend.inf"
echo "== registry.inf: Run / RunOnce / Winlogon / Shell lines"
grep -n -i -E 'CurrentVersion\\Run|Winlogon|"Shell"|\[AddReg|\[Version' "$tmp/registry.inf" | head -n 40 || true
echo "== registry.inf: first 25 lines"
head -n 25 "$tmp/registry.inf"
