#!/bin/bash
set -e
P=$HOME/summer26/PufferLib
INC="-I $P/raylib-5.5_macos/include -I $P/src -I $P/vendor"
RL=$P/raylib-5.5_macos/lib/libraylib.a
FW="-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL"
SAN="-fsanitize=address,undefined"
gcc -g -O0 -Wall -Wextra -DDEBUG $SAN $INC openfront.c -o of_dbg  $RL $FW -lm
gcc -g -O0 -Wall -Wextra         $SAN $INC drive_test.c -o drive  $RL $FW -lm
gcc -O2 -Wall -Wextra                 $INC openfront.c -o of_fast $RL $FW -lm
echo "built of_dbg + drive + of_fast"
