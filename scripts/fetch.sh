#!/usr/bin/env bash
# Downloads ReactOS and the bundled apps into ./cache. Safe to re-run: cached files are reused.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck source=../kit/sources.env
source kit/sources.env
mkdir -p cache
UA="Mozilla/5.0 (avocadOS build)"

dl() { # url outfile
    local url="$1" out="$2"
    if [ -s "$out" ]; then echo "cached: $out"; return; fi
    echo "download: $url"
    curl -fL --retry 4 --retry-delay 5 -A "$UA" -o "$out.part" "$url"
    mv "$out.part" "$out"
}

find_link() { # index_url regex -> prints the full url of the first matching file
    local idx="$1" re="$2" html name
    html=$(curl -fsL --retry 3 -A "$UA" "$idx")
    name=$(printf '%s' "$html" | grep -oE "$re" | head -n1 || true)
    if [ -z "$name" ]; then echo "no file matching $re at $idx" >&2; exit 1; fi
    echo "${idx%/}/$name"
}

# --- ReactOS (the install CD, not the live CD) ---
if ! ls cache/reactos/*.iso >/dev/null 2>&1; then
    zipname=""
    # SourceForge's folder page is the first choice; its RSS feed is the fallback.
    for listing in \
        "https://sourceforge.net/projects/reactos/files/ReactOS/${REACTOS_VERSION}/" \
        "https://sourceforge.net/projects/reactos/rss?path=/ReactOS/${REACTOS_VERSION}"; do
        echo "looking for the ReactOS ${REACTOS_VERSION} install ISO at $listing"
        page=$(curl -sSL --retry 3 -A "$UA" -w '\nHTTP %{http_code}\n' "$listing" || true)
        echo "$page" | tail -n1
        zipname=$(printf '%s' "$page" | grep -oE 'ReactOS-[A-Za-z0-9._-]*-iso\.zip' \
            | grep -viE 'live|dbg' | head -n1 || true)
        [ -n "$zipname" ] && break
        echo "no iso zip name found; names that look like files on that page:" >&2
        printf '%s' "$page" | grep -oE '[A-Za-z0-9._-]+\.(zip|7z|iso)' | sort -u | head -n 20 >&2 || true
    done
    if [ -z "$zipname" ]; then echo "could not find a ReactOS ${REACTOS_VERSION} iso zip" >&2; exit 1; fi
    echo "using $zipname"
    dl "https://sourceforge.net/projects/reactos/files/ReactOS/${REACTOS_VERSION}/${zipname}/download" "cache/$zipname"
    mkdir -p cache/reactos
    unzip -o -j "cache/$zipname" '*.iso' -d cache/reactos
fi
ls -lh cache/reactos

# --- bundled apps ---
dl "$FIREFOX_URL" cache/firefox.exe
dl "$(find_link "$VLC_INDEX" "$VLC_REGEX")" cache/vlc.exe
dl "$(find_link "$LIBREOFFICE_INDEX" "$LIBREOFFICE_REGEX")" cache/libreoffice.msi
dl "$PYTHON_URL" cache/python.msi

ls -lh cache
