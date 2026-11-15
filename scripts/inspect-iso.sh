#!/usr/bin/env bash
# Prints the layout of an ISO's \reactos folder and any Run/RunOnce registry hooks, for debugging.
set -euo pipefail
ISO=$1
echo "== top level"
xorriso -indev "$ISO" -find / -maxdepth 1 2>/dev/null | head -n 40
echo "== /reactos"
xorriso -indev "$ISO" -find /reactos -maxdepth 1 2>/dev/null | head -n 60
tmp=$(mktemp -d)
for f in hivesft.inf hivedef.inf hivesys.inf; do
    if xorriso -osirrox on -indev "$ISO" -extract "/reactos/$f" "$tmp/$f" >/dev/null 2>&1; then
        echo "== $f: $(wc -l < "$tmp/$f") lines; lines mentioning Run or Winlogon"
        grep -n -i -E 'CurrentVersion\\Run|Winlogon|Shell' "$tmp/$f" | head -n 15 || true
    else
        echo "== $f: not found as a plain file"
    fi
done
