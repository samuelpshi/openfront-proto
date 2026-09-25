# summer26 — OpenFront RL env

**Before starting work, read `docs/openfront_project_reference.md` §1.** Current
state, the baseline (and its sha) and next steps live there and nowhere else.
`docs/history.md` is an archive; don't load it.

## Repos (two, don't confuse them)

- `~/summer26/PufferLib` — fork `samuelpshi/PufferLib`, branch `5.0`.
  The env is **header-only**: `ocean/openfront/openfront.h` + `config/openfront.ini`.
  No `binding.c`, no `openfront.c` — `src/puffercpu.c` includes the header directly.
  This header is canonical. Never create a second copy.
- `~/summer26/openfront` — `samuelpshi/openfront-proto`. Dev tools only:
  `harness.c`, `drive_test.c`, `mk.sh`, `cxxcheck.sh`. No env source lives here;
  `mk.sh` sets `P=$HOME/summer26/PufferLib` and compiles the header from the fork.

Work from `~/summer26` so both are visible.

## Build loop

```
cd ~/summer26/openfront && ./mk.sh && ./cxxcheck.sh && ./of_dbg
```

`mk.sh` builds `of_dbg` (tests + `check_borders` + `hist_run`), `drive`, `of_fast`.
`cxxcheck.sh` is **not optional**: `nvcc` compiles the header as C++ while `mk.sh`
compiles it as C. Two defects have already shipped through that gap. C99 VLAs are
the most common offender — they compile as C and fail as C++.

Framework build: `cd ~/summer26/PufferLib && ./build.sh openfront --cpu`.

The Mac cannot train. All training and eval is Vast (RTX 3090, verified hosts).

## Terrain encoding

`terrain[]` is a packed byte, matching OpenFrontIO's layout:

```
bit 7  land
bit 6  shoreline   (set on land AND water tiles; guard with is_land)
bit 5  ocean       (on water tiles; distinguishes ocean from lake)
bits 0-4  magnitude 0-31   (31 = impassable, not generated yet)
```

**Never compare `terrain[t]` directly.** Water carries the ocean bit, so water is
not zero. Use `is_land` / `is_ocean` / `is_shoreline` / `magnitude` / `terrain_type`.
`is_shore` = land && shoreline. `is_ocean_shore` = land && any 4-neighbour ocean,
computed live, not a bit. Terrain types derive from magnitude: <10 Plains,
<20 Highland, <31 Mountain.

**Land magnitude is elevation. Water magnitude is distance to land.** Easy to get
backwards. Authority is `packTerrain` in `map-generator/map_generator.go` in the
OpenFrontIO repo: land packs as `ceil(magnitude)` from `(Blue - 140) / 2` clamped
0-30, water packs as `ceil(magnitude / 2)` where magnitude is manhattan distance to
nearest land. `WaterManager.ts`'s `ceil(dist_to_coast / 2)` is the *water* rule;
reading it out of context gives the wrong answer for land.

Impassable packs as `0b10011111` and is excluded from `numLandTiles` — relevant to
the `perf` denominator when it lands.

## Style

`~/summer26/PufferLib/SKILL_ISSUES.md` is Joseph's published style and refactoring
guide. New code is written to it: preallocate at init, asserts over defensive
checks, no forward declarations, headers are source not declaration lists,
soft 80 / hard 100 col, 4-space indents, don't split into more files.

## Framework constraints

- `struct Log` must have `float perf` first and `float n` last. The framework casts
  `Log` to a flat float array and divides every field by `n`
  (`src/puffercpu.c:953-962`), so counters read as fractions of episodes.
- `sizeof(Env)` is a budget — `src/pufferl.cu:1000` does
  `calloc(total_agents, sizeof(Env))`, then line 1008 `realloc`s down to
  `num_envs * sizeof(Env)` where `num_envs = total_agents / num_agents`.
  Large per-env arrays go behind pointers. Current size: reference §1; scaling: §6.3.
- `envs[i].rng` is assigned the env index **before** `puf_init` runs. Seed
  scrambling is mandatory.
- Macros are `OF_W` / `OF_H` / `OF_N`. Unscoped names collided with upstream.
  `simplex.h` declares `GRAD3` / `GRAD4` / `PERM` unprefixed — same hazard.

## Conventions & gotchas

- Comparisons: use `cmp` (with exit code) and `shasum -a 256`. Never diff exit
  status.
- Hashes are sha256 of `of_dbg`'s **stdout**
  (`./of_dbg > out.txt; shasum -a 256 out.txt`), never of the binary. ld64
  embeds a random LC_UUID and ad-hoc signature per link, so binary hashes
  change on every rebuild.
- FP contraction: `openfront.h` opens with `#pragma STDC FP_CONTRACT OFF` and
  closes with `#pragma STDC FP_CONTRACT DEFAULT`. Never add `-ffp-contract`
  or `-ffast-math` to `build.sh`. `mk.sh` carries `-ffp-contract=off`.
  Rationale: reference §4.
- No libm transcendentals in `openfront.h` — use
  `det_exp`/`det_log`/`det_pow`/`det_atan2`. `floor`/`sqrt` are fine (exact).
- Warnings: `mk.sh` and `cxxcheck.sh` carry `-Wfloat-conversion
  -Wimplicit-float-conversion`. Apple clang's `-Wfloat-conversion` alone does
  NOT catch double->float narrowing. Open hits: reference §5.3.
- Numeric model (spec §2, reference §3): sim math is `double`. Player troops are
  `int64_t`, written only through `troops_set`/`troops_add`/`troops_remove` --
  never assign `players[p].troops` directly. Attack troops stay `double`,
  clamped at 0 on every write (spec §7.3). Heap priorities stay `float` by design.
- On the Mac, run `/opt/homebrew/bin/bash ./build.sh openfront --cpu` (bash >= 4
  required; `/bin/bash` 3.2 fails on `${ENV^^}`). Reference §2.
- x86 recheck bundles are built from the **committed** tree, with the Mac
  stdout and a MANIFEST (file hashes + fork HEAD). Never hand-copy a header
  into a bundle.

## Verification discipline

A refactor is not verified until a one-variable control is **bit-identical**.
"Direction and rough magnitude match" is where a transposed argument hides.

Behaviour-neutral = unchanged `of_dbg` stdout sha. The baseline and its sha are
in reference §1. Behaviour-changing commits: `./sweep.sh <before-dir> <after-dir>`,
20 seeds unpaired, |Δ| < 2 SE (reference §4).

**Precision changes can't be bit-identical**, so they need a different control:
(1) lockstep trace of episode 0 against the previous build,
per-tick troops at `%.17g` -- relative diff should sit at float ULP level until
the first tile or attack-set divergence; normalise attack troops by the attack's
start troops, not current troops (near-zero residuals fake large relative
errors). Report which code paths fired inside the window. (2) Multi-seed
`hist_run` (seeds 42-46) on both builds; the delta must sit inside one
seed-to-seed sd. A transposed argument shows up as a first-tick jump in (1).

Report divergence from a stated acceptance target. Do not paper over it, and do not
adjust the target to match the output. `isolation_test` must stay bit-identical
across 3 envs x 3 episodes.

`hist_run`'s 2000 ticks equals training's `max_steps 200 x action_repeat 10`, so
its numbers are directly comparable to the dashboard.

When changing an encoding, enumerate every **write** as well as every read. A
partial migration that leaves comparisons against the old representation is worse
than a no-op — it silently inverts them.

## Working convention

Sam audits everything; explain the reasoning, not just the patch (reference §7).

Edit rules, every time:

- Read the signature, not the call site. Transposition traps: `heap_push(e, h,
  tile, pri)` is tile first; `conquer(e, p, t)` is player first;
  `rng_int(e, lo, hi)` is `[lo, hi)`. Tiles (`t`, `nb[k]`) and players (`p`,
  `attacker`, `target`, `owner[...]`) are all bare ints: the compiler won't help.
- Hand over whole functions, never excerpts.
- Multi-site edits go through a script that asserts each anchor matches exactly
  once.
- `./mk.sh && ./cxxcheck.sh` must both pass before any push.
- Never `git add -A` in the fork (`~/summer26/PufferLib`): it carries another
  session's uncommitted edits.
- C bug classes to watch: `=` vs `==`, `.` vs `->`, struct by value vs pointer,
  missing braces/return, integer division, `continue` in a nested loop, C99 VLAs.
- Code that is never executed is not verified: add the thing that exercises a
  new interface in the same pass.

## Reference docs

`docs/openfront_env_spec.md` — mechanics; wins on mechanics disputes.
`docs/openfront_project_reference.md` — state (§1), tooling (§2), header notes
  (§3), decisions and rescales (§4), open items (§5), training (§6, §6.3 for map
  size and `sizeof(Env)` scaling), conventions (§7), claim discipline (§8).
Upstream source of truth: `github.com/openfrontio/OpenFrontIO`, AGPL-3.0.
Mechanics are derived, never transliterated.

## Annexation and terrain-entry rules

- `annex_surrounded`: the largest path gates on `is_ocean_shore`
  (PlayerExecution.ts:374), the others on `is_shore` (:432). On the largest path
  the gate is redundant (any lake-shore tile has an unowned water 4-neighbour, and
  that path bails on owner==0) but is kept for source fidelity.
- `annex_shore_test` uses real geometry only: never set a shore bit without a
  water 4-neighbour.
- Do not touch `annex_enclosed` (`!is_land` already matches isEnclosed treating
  lakes as exits).
- Owned => land (reference §4). `conquer`'s water check is a DEBUG trap only;
  every new tile-taking path filters `is_land` at entry. All territory
  denominators use `land_tiles`; TN predicates are land-gated.
- Spawn sweep: `./of_fast spawn` (seeds 0-999, 8 players, land_frac=0.65).
