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
# clang-18 warning list for the bundled (committed) header, compiled as C the way mk.sh does
C18=/opt/homebrew/opt/llvm@18/bin/clang
[ -x "$C18" ] || { echo "clang-18 missing: brew install llvm@18"; exit 1; }
(cd "$B" && "$C18" --version | head -1 > clang18_warnings.txt && \
    "$C18" -fsyntax-only -Wall -Wextra -Wconversion -Wfloat-conversion -Wimplicit-float-conversion \
    -DDEBUG -I ocean/openfront -I src -I "$F/vendor" -I "$F/raylib-5.5_macos/include" harness.c \
    >> clang18_warnings.txt 2>&1)
echo "clang-18 warnings: $(grep -c ': warning:' "$B/clang18_warnings.txt")"
(cd "$B" && shasum -a 256 $(find . -type f) > MANIFEST.txt)
git -C "$F" rev-parse HEAD >> "$B/MANIFEST.txt"
rm -f ~/Downloads/of_x86check.zip && (cd /tmp && zip -qr ~/Downloads/of_x86check.zip of_x86check)
shasum -a 256 "$B/after_mac.txt"
