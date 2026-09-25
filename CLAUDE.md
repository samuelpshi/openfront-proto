# summer26 — OpenFront RL env

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
  Currently 948,488 bytes (~948 KB). Large per-env arrays go behind pointers.
- `envs[i].rng` is assigned the env index **before** `puf_init` runs. Seed
  scrambling is mandatory.
- Macros are `OF_W` / `OF_H` / `OF_N`. Unscoped names collided with upstream.
  `simplex.h` declares `GRAD3` / `GRAD4` / `PERM` unprefixed — same hazard.

## Conventions & gotchas

- Comparisons: use `cmp` (with exit code) and `shasum -a 256`. Never diff exit
  status.
- Baseline hashes are sha256 of `of_dbg`'s **stdout**
  (`./of_dbg > out.txt; shasum -a 256 out.txt`), never of the binary. ld64
  embeds a random LC_UUID and ad-hoc signature per link, so binary hashes
  change on every rebuild.
- FP contraction: `openfront.h` opens with `#pragma STDC FP_CONTRACT OFF` and
  closes with `#pragma STDC FP_CONTRACT DEFAULT`. Never add `-ffp-contract`
  or `-ffast-math` to `build.sh`. `mk.sh` carries `-ffp-contract=off`.
  Since 2a the pragma is load-bearing: without it, `-ffp-contract=fast`
  changes `hist_run` output.
- No libm transcendentals in `openfront.h` — use
  `det_exp`/`det_log`/`det_pow`/`det_atan2`. `floor`/`sqrt` are fine (exact).
- Warnings: `mk.sh` and `cxxcheck.sh` carry `-Wfloat-conversion
  -Wimplicit-float-conversion`. Apple clang's `-Wfloat-conversion` alone does
  NOT catch double->float narrowing. Known out-of-scope hit: `float lf =
  dict_get(...)` in `puf_init` (fix to `double` in a cleanup commit).
- Numeric model: sim math is `double` (Tier A #2a). Player troops become
  `int64_t` in 2b, written only through `troops_set`/`troops_add`/
  `troops_remove` -- never assign `players[p].troops` directly. Attack troops
  stay `double`, clamped at 0 on every write (spec §7.3). Heap priorities stay
  `float` by design (every key value is exact in float).
- On the Mac, run `bash ./build.sh`, not `./build.sh` (`/bin/bash` 3.2).
- x86 recheck bundles are built from the **committed** tree, with the Mac
  stdout and a MANIFEST (file hashes + fork HEAD). Never hand-copy a header
  into a bundle.

## Verification discipline

A refactor is not verified until a one-variable control is **bit-identical**.
"Direction and rough magnitude match" is where a transposed argument hides.

Baseline (Tier A complete, PufferLib 5.0 @ 4a3d2848, Mac arm64 == x86_64):
hist_run(300, 2000, 42): wins 54 (18.0%), mean length 1920, eliminated 64.2%
annexations 3857 (12.86/ep), tiles moved 29773 (7.7/event)
spawn failures 0, heap peak 209/2048, heap drops 0
sizeof(Env) = 985352
of_dbg stdout sha256 bce152dee0f18893e17e7b06ac7e0635ad94b318d1e99663debb30156f1d2c0d
Behaviour-changing commits: ./sweep.sh <before-dir> <after-dir>, 20 seeds unpaired, |Δ| < 2 SE.

**Precision changes can't be bit-identical**, so they need a different control
(protocol from 2a): (1) lockstep trace of episode 0 against the previous build,
per-tick troops at `%.17g` -- relative diff should sit at float ULP level until
the first tile or attack-set divergence; normalise attack troops by the attack's
start troops, not current troops (near-zero residuals fake large relative
errors). Report which code paths fired inside the window. (2) Multi-seed
`hist_run` (seeds 42-46) on both builds; the delta must sit inside one
seed-to-seed sd. A transposed argument shows up as a first-tick jump in (1).

Report divergence from a stated acceptance target. Do not paper over it, and do not
adjust the target to match the output. `isolation_test` must stay bit-identical
across 3 envs x 3 episodes.

`hist_run`'s 2000 ticks equals training's `max_steps 200 x action_repeat 10`, so the
numbers are directly comparable to the dashboard. Bots-only resolution was 24.7% pre-Tier-A
(5.0% pre-map-gen; mostly because the 0.8 land-share win bar shrank with land
area); it moves with every Tier A commit. Do not compare agent win rate against the old 0.086.

When changing an encoding, enumerate every **write** as well as every read. A
partial migration that leaves comparisons against the old representation is worse
than a no-op — it silently inverts them.

## Working convention

Sam audits everything. Joseph reviews contributor env PRs on stream and asks
implementation questions, so anything Sam can't explain unprompted isn't done.
Explain the reasoning, not just the patch.

## Reference docs

`openfront_env_spec.md` — mechanics; wins on mechanics disputes.
`openfront_project_reference.md` — state, decisions, training/trainer ops,
  §6.4 for map size and `sizeof(Env)` scaling.
Upstream source of truth: `github.com/openfrontio/OpenFrontIO`, AGPL-3.0.
Mechanics are derived, never transliterated.

## Steps

Step 4 (shore split) — DONE, behaviour-neutral.
`annex_surrounded`: largest path gates on `is_ocean_shore` (PlayerExecution.ts:374),
others on `is_shore` (:432).
On the largest path the gate is redundant: any lake-shore tile has an unowned water
4-neighbour, and the largest path bails on owner==0 (source does the same). Kept for
source fidelity; hist_run bit-identical.
`annex_shore_test` uses real geometry only — never set a shore bit without a water
4-neighbour.
Do not touch `annex_enclosed` (`!is_land` already matches isEnclosed treating lakes
as exits).

Step 5 (spawn sweep) — status: DONE, no change. Land pinned at 1498 by quantile threshold; largest
component 1491-1498. 0/8000 hard failures, 6.9% of maps relax once, max
depth 1. Spawn constants unchanged. spawn_sweep copy verified tile-identical
to sim_reset over seeds 0-999.
`./of_fast spawn` — seeds 0-999, 8 players, land_frac=0.65.
```
hard spawn failures: 0/8000 slots (0.00%)
maps with any relaxed spawn: 69/1000 (6.9%)
relaxation depth histogram: depth=0 931 maps, depth=1 69 maps
total spawn attempts: 275891 (mean 275.9/map, mean 34.5/player)
largest-comp size: min=1491  p1=1495  median=1498
5 worst maps (relax_spawns DESC, comp_size ASC):
  seed  429: hard=0 relax=2 depth=1 tries=2015 comp=1498
  seed  330: hard=0 relax=1 depth=1 tries=984  comp=1496
  seed  718: hard=0 relax=1 depth=1 tries=928  comp=1496
  seed  881: hard=0 relax=1 depth=1 tries=893  comp=1496
  seed   10: hard=0 relax=1 depth=1 tries=1127 comp=1497
```

Step 6 (terrain audit) — status: DONE. Invariant: owned => land, by induction — only
unowned-tile entries are attack seed/refill (is_land filtered) and spawn disk
(spawn_disk_ok: bounds, land, unowned). conquer's water check is a DEBUG trap only.
All territory denominators use land_tiles. TN predicates land-gated. Removed two
dead is_land guards (atk_push, attack_tick pop). Style note for pre-PR pass: spawn
disk shape duplicated in spawn_disk_ok and spawn_place.

Step 7 (training on terrain) — DONE. 100M on terrain. 1x128 perf 0.330 (mean, seeds
73/74) vs 0.315 open grid — terrain did not move the ceiling. 2x512 perf 0.348,
adopted (no SPS cost). Eliminated ~45% on all runs regardless of capacity: ceiling
is obs. Seed variance ~±0.013 perf; single-run deltas <0.03 are noise. Win rates not
comparable across the terrain change.
Policy baseline is now `hidden_size 512`, `num_layers 2`.

Plan status: map-gen steps 1-7 done. Spatial obs is deferred to the PR after
PR #1.

**PR #1 scope (decided 24 Sept): Tier A + Tier B-lite** -- gold income, City,
Defense Post (spec §0). Factories, nukes, naval, diplomacy are later PRs. Open
decisions for B-lite are listed in spec §0; don't start B-lite code until they're
answered.

Tier A progress (spec §25):
- #1 DetMath + FP_CONTRACT -- done 21 Sept.
- `297cad49` tests use local xorshift, not libc `rand()`.
- `1f3eae15` attack troops clamped at 0 (spec §7.3) + DEBUG invariant.
- #2a float -> double sim math -- done `9f303628`, verified Mac/x86.
- Next: harness prints `env_hash` per episode (dev repo, before 2b); then
  #2b int64 player troops; then combat rewrite, annexation, attack init,
  dead-defender check, spawn disk/phase, bot river crossing.
- Flip the header comment `fc50009` -> `7defd24` in the last Tier A commit.
