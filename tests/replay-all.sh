#!/bin/bash
# replay-all.sh - run replay.exe on every pcx2session result archive in a
# folder (.zip, .tar.gz, .tgz, or already-unpacked folders with a
# report.txt) and collect the results in one file.
#
# Usage: bash replay-all.sh FOLDER [results.txt]
#
# Results go to FOLDER/replay-results.txt unless another file is named. A
# folder with the same name as an archive beside it (an unpacked copy) is
# skipped, so each session is counted once.
#
# replay.exe and sgl.dll must be next to this script, and Wine installed.
# Each archive is unpacked into a temporary folder that is removed again.
#
# SPDX-License-Identifier: MIT
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?usage: bash replay-all.sh FOLDER [results.txt]}
SRC=$(cd "$SRC" && pwd)
OUT=${2:-$SRC/replay-results.txt}
command -v wine >/dev/null || { echo "Wine is not installed"; exit 1; }
[ -f "$HERE/replay.exe" ] && [ -f "$HERE/sgl.dll" ] || { echo "replay.exe and sgl.dll must be next to this script"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
: > "$OUT"
n_sessions=0; n_cases=0; n_diff=0

run_one() { # $1 = label, $2 = folder holding report.txt
    local res
    res=$(cd "$HERE" && WINEDEBUG=-all wine replay.exe "$(winepath -w "$2" 2>/dev/null)" 2>/dev/null | tr -d '\r')
    { echo "=== $1"; echo "$res"; echo; } >> "$OUT"
    echo "$res" | tail -1 | sed "s|^|$1: |"
    n_sessions=$((n_sessions + 1))
    local c d
    c=$(echo "$res" | sed -n 's/^\([0-9]*\) cases, \([0-9]*\) with differences$/\1/p')
    d=$(echo "$res" | sed -n 's/^\([0-9]*\) cases, \([0-9]*\) with differences$/\2/p')
    n_cases=$((n_cases + ${c:-0})); n_diff=$((n_diff + ${d:-0}))
}

shopt -s nullglob
for a in "$SRC"/*.zip "$SRC"/*.tar.gz "$SRC"/*.tgz "$SRC"/*/; do
    label=$(basename "$a")
    if [ -d "$a" ]; then
        label=$(basename "$a")
        if [ -e "$SRC/$label.zip" ] || [ -e "$SRC/$label.tar.gz" ] || [ -e "$SRC/$label.tgz" ]; then
            continue    # unpacked copy of an archive already counted
        fi
        rep=$(find "$a" -name report.txt | head -1)
    else
        work="$TMP/$label"; rm -rf "$work"; mkdir -p "$work"
        case "$a" in
            *.zip) unzip -q "$a" -d "$work" ;;
            *)     tar xzf "$a" -C "$work" ;;
        esac
        rep=$(find "$work" -name report.txt | head -1)
    fi
    if [ -z "$rep" ]; then
        echo "$label: no report.txt, skipped" | tee -a "$OUT"
        continue
    fi
    run_one "$label" "$(dirname "$rep")"
    [ -d "$a" ] || rm -rf "$work"
done

echo "Total: $n_sessions sessions, $n_cases cases, $n_diff with differences" | tee -a "$OUT"
echo "Full results in $OUT"
