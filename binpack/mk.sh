#!/bin/bash
# Linux sandbox paths; on the Mac swap RLD for $P/raylib-5.5_macos and add the Cocoa frameworks as in ../mk.sh
set -e
P=${P:-$HOME/summer26/PufferLib}
RLD=${RLD:-$P/raylib-5.5_macos}
INC="-I $RLD/include -I $P/src -I $P/vendor -I $P/ocean/binpack"
RL="$RLD/lib/libraylib.a"
if [ "$(uname)" = "Darwin" ]; then LIBS="$RL -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -lm"
else LIBS="$RL -lm -lpthread -ldl"; fi
W="-Wall -Wextra -Wfloat-conversion -ffp-contract=off"
gcc -g -O0 $W -DDEBUG -fsanitize=address,undefined $INC bp_test.c -o bp_dbg $LIBS
gcc -O2 $W $INC bp_test.c -o bp_fast $LIBS
g++ -fsyntax-only -x c++ -std=c++17 -Wall -Wfloat-conversion $INC $P/ocean/binpack/binpack.h
echo "built bp_dbg + bp_fast, c++ ok"
