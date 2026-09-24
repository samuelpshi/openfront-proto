#!/bin/bash
# usage: ./sweep.sh <dirA> <dirB>   (dirs containing openfront.h + simplex.h)
set -e
P=~/summer26/PufferLib
RL="$P/raylib-5.5_macos/lib/libraylib.a -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL"
FL="-O2 -ffp-contract=off -I $P/raylib-5.5_macos/include -I $P/src -I $P/vendor"
gcc $FL -I "$1" harness.c -o /tmp/sw_a $RL -lm 2>/dev/null
gcc $FL -I "$2" harness.c -o /tmp/sw_b $RL -lm 2>/dev/null
for b in a b; do for s in $(seq 42 61); do
  echo "$b $(/tmp/sw_$b hist $s | grep -E '^episodes|^annex' | tr '\n' ' ')"
done; done > /tmp/sweep.txt
python3 - <<'PY'
import re, statistics as st
d = {"a": [], "b": []}
for l in open("/tmp/sweep.txt"):
    d[l.split()[0]].append([int(re.search(p, l)[1]) for p in (r"wins (\d+)", r"annexations (\d+)", r"tiles moved (\d+)")])
for i, n in enumerate(["wins", "annex", "tiles"]):
    A = [r[i] for r in d["a"]]; B = [r[i] for r in d["b"]]
    se = (st.variance(A)/len(A) + st.variance(B)/len(B)) ** .5
    dm = st.mean(B) - st.mean(A)
    print(f"{n:6s} A {st.mean(A):9.1f}  B {st.mean(B):9.1f}  Δ {dm:+8.1f}  SE {se:6.1f}  t {dm/se:+.2f}")
PY
