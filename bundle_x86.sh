#!/bin/bash
# Build the x86 recheck bundle from the COMMITTED tree -> ~/Downloads/of_x86check.zip
set -e
F=$HOME/summer26/PufferLib
D=$HOME/summer26/openfront
B=/tmp/of_x86check
git -C "$F" diff --quiet HEAD -- ocean/openfront/ || { echo "uncommitted changes in ocean/openfront — commit first"; exit 1; }
rm -rf "$B" && mkdir -p "$B/src" "$B/ocean/openfront"
cp "$D/harness.c" "$D/mk.sh" "$B/"
cp "$F/ocean/openfront/openfront.h" "$F/ocean/openfront/simplex.h" "$B/ocean/openfront/"
cp "$F/src/pufferenv.h" "$F/src/ini.h" "$B/src/"
(cd "$D" && ./mk.sh >/dev/null && ./of_dbg > "$B/after_mac.txt")
(cd "$B" && shasum -a 256 $(find . -type f) > MANIFEST.txt)
git -C "$F" rev-parse HEAD >> "$B/MANIFEST.txt"
rm -f ~/Downloads/of_x86check.zip && (cd /tmp && zip -qr ~/Downloads/of_x86check.zip of_x86check)
shasum -a 256 "$B/after_mac.txt"
