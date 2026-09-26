# OpenFront Env — Project Reference

Companion to `openfront_env_spec.md`. The spec is the **mechanics** reference: formulas, constants and rules derived from the source. This file is the **project** reference: current state, tooling, settled decisions, open items, conventions and claim discipline. When the two disagree on mechanics, the spec wins.

This file records **current state only**. Session narratives, superseded baselines and closed investigations live in `docs/history.md`, which is not loaded into chats. When something changes, edit it in place and append the story to history. Don't leave SUPERSEDED banners here.

Last updated: **25 Sept 2026 — Tier B-lite code complete** (`e27e24f3`).

---

## 1. Current state

The env lives in `ocean/openfront/openfront.h` and `config/openfront.ini` on `samuelpshi/PufferLib`, branch `5.0`. It trains end to end on a Vast 3090. The header conforms to upstream `7defd24` for the Tier A scope, the territorial core (spec §25). **Nothing has been trained on the Tier A build yet.** The policy baseline is 2×512. The last training result (21 Sept, pre-Tier-A) put the ceiling at observation, not terrain and not capacity (§6).

**Tier B-lite is code complete** at `e27e24f3`: gold, City and Defense Post, build actions (Discrete-13), automatic placement, 39-field obs and build logging. **Next:** push, then a baseline retrain (100M, two seeds; watch-list in §5.2), then the draft PR. PR #1 is Tier A plus B-lite (spec §0).

**Tier A commits on `5.0`, oldest first.** These follow the clamp `eea12848` and the 2a commit `39db150f`.

| Commit | Change |
|---|---|
| `84ebc825` | #2b, `int64_t` player troops |
| `5175a70e` | 52-tile spawn disk |
| `908b785a` | #1a, attack border set |
| `c4faca4a` | #1b, combat formulas |
| `9b542489` | #4a, annex capturer |
| `d6aef90c` | #4b, hole-aware largest cluster |
| `4a3d2848` | header anchor `fc50009` → `7defd24` |
| `fa64934c` | perf: LTB lookup table |
| `3e26237d` | perf: troop-cap pow table |
| `7b4e6d01` | B-lite 1: gold (income, `conquerPlayer` transfer, death zeroing) |
| `61a71536` | B-lite 2a: structures (City, DefensePost): cost, capture, City bonus, post modifier, bot scrapping |
| `e27e24f3` | B-lite 2b: build actions (Discrete-13), automatic placement, obs 39, build log |

**History was rewritten once.** `5.0` was `filter-branch`ed to fix the author email. Two hashes changed: `1f3eae15` → `eea12848` and `9f303628` → `39db150f`; `297cad49` kept its hash. The trees are identical. Use the new hashes everywhere.

**Dev repo:** `samuelpshi/openfront-proto`, with `docs/` and `CLAUDE.md` tracked.

**Baseline: the acceptance target for any behaviour-neutral change.** It comes from `hist_run(300, 2000, 42)` at `e27e24f3`, and stdout includes the per-episode `ep … env … map …` lines. The sim trajectory hasn't changed since the perf pass: across `7b4e6d01`, `61a71536` and `e27e24f3` every episode's length and map hash is identical to `3e26237d`'s (see the protocol in §4 for how B-lite was checked). **x86:** Linux x86_64 clang-18.1.3 at `-O0`, `-O2` and ASan/UBSan, all `cmp`-identical to the Mac stdout; the clang-18 warning set is identical (42, 23 in the header).

```
wins 54 (18.0%), mean length 1920, eliminated 64.2%
annexations 3857 (12.86/ep), tiles moved 29773 (7.7/event)
spawn failures 0, heap peak 209/2048, heap drops 0
sizeof(Env) = 1044816
of_dbg stdout sha256 043b39322ea75e9ce2fd45982e9c30d78c6472bc5bfc469860992c6c02ccccb7
```

Single-seed wins swing by ±9 with nothing changed (seed sd ≈ 9 over 20 seeds). Read behaviour changes off `sweep.sh`, never off this block. Don't compare win rates across the terrain change: the 0.8 land-share bar fell from ~1693 tiles to ~1198.

**Throughput:** ~830k ticks/sec (`of_fast bench`, M3 Pro, `-O2`, median of 5, interleaved A/B) after the perf pass; ~724–750k before it. Bench-bisect attributed the Tier A loss to DetMath (−8.7%, `61514968`) and 1b (−9.9%, `c4faca4a`). The two table commits recovered 1b fully (+12.3% vs −9.9%) and DetMath partly (~+3% vs −8.7%; the growth `det_pow` remains). An earlier ~525k figure was not reproduced: reruns of the same commit gave ~724–750k. Likely measured under load. The 1.02M `phase1-baseline` predates map gen (different workload) and is not comparable. **B-lite benches (25 Sept) are relative only:** `BTLEServer` pinned a core, and `3e26237d` itself read 616k against the recorded ~830k. Structures (`61a71536`) −2.9% vs `3e26237d`, inside the ±3% noise band; commit 2 (`e27e24f3`) +0.2% vs `61a71536`. Re-bench on a quiet machine before quoting an absolute.

**Map:** procedural simplex, 48×48, land pinned at exactly **1498** tiles (`2304 − (int)(0.35 × 2304)`). The terrain split over 200 maps is Plains 61.4 / Highland 32.1 / Mountain 6.5, with 1.2 land components and 5.2 lakes on average. Islands and lakes are kept, and spawns are restricted to the largest land component. A 1000-seed × 8-player spawn sweep gave 0 hard failures; 6.9% of maps relax min-distance once, with max depth 1.

---

## 2. Build & tooling

**One canonical header.** `openfront.h` lives at `~/summer26/PufferLib/ocean/openfront/openfront.h` and nowhere else. The dev tools (`harness.c`, `drive_test.c`, `mk.sh`, `cxxcheck.sh`, `sweep.sh`) live in `~/summer26/openfront` and reach the header through `-I`. A duplicated header drifts, and then the file that gets audited isn't the file that trains. If a second copy ever appears, delete it rather than syncing it.

**`mk.sh`** builds three binaries: `of_dbg` (`-O0 -DDEBUG`, ASan+UBSan, which runs the tests, `check_borders()` and `hist_run`), `drive` (the binding driver) and `of_fast` (`-O2` bench). It passes `-ffp-contract=off -Wfloat-conversion -Wimplicit-float-conversion`. The header needs the fork present to compile, because it includes `pufferenv.h` and links the fork's static raylib for `puf_render`.

**Through the framework** (from `~/summer26/PufferLib`):

```bash
/opt/homebrew/bin/bash ./build.sh openfront --cpu   # -> ./openfront: eval main + raylib render. Mac OK.
./build.sh openfront                                 # -> ./puffer: the trainer. Needs nvcc. Vast only.
```

**Build points:**

- **`-DDEBUG` is load-bearing.** Without it `run_tests()` and `check_borders()` compile to nothing and the program passes silently. VS Code's default clang task lacks it.
- **`-lm` is required** (`floor`, `sqrtf`). Missing it is a link error.
- **`mk.sh` passes `-ffp-contract=off`; `build.sh` doesn't and mustn't.** In the framework build, the header's own `#pragma STDC FP_CONTRACT OFF` does the job (§4).
- **Homebrew bash is needed on the Mac until the bash-3 PR merges.** `/bin/bash` 3.2 fails on `${ENV^^}`.
- **`ccache` is a hard dependency of the `native` build.** Without it, the build dies at `line 506: ccache: command not found`. `brew install ccache` locally, and check the Vast image.
- **The header is compiled as C locally and as C++ by nvcc.** `mk.sh` can't see the C++ side, so run `cxxcheck.sh` (`g++ -fsyntax-only -x c++ -std=c++17` on the header) after any header change; both must pass before pushing. It proves language-level C/C++ compatibility. It doesn't prove link errors, nvcc diagnostics or macro collisions from upstream including our header. Likely future offenders are designated initializers, implicit `void*` casts and enum arithmetic. Static asserts go through `OF_STATIC_ASSERT`. The one `#pragma once in main file` warning is harmless.
- **Warnings are stricter than upstream.** The code is clean under `-Wall -Wextra` in both modes. Upstream's `build.sh` uses `-Wall` plus a few `-Werror=` promotions and no `-Wextra`. Keep the stricter bar.
- **`sweep.sh <dirA> <dirB>` is the acceptance test for behaviour-changing commits.** It builds each header dir at `-O2` and runs `hist <seed>` for seeds 42–61 (300 episodes each). It prints mean A, mean B, Δ, SE and t for wins, annexations and tiles moved. Build the "before" dir with `git show HEAD:ocean/openfront/{openfront,simplex}.h`. Running a dir against itself is a free null control.
- **`harness.c` modes.** `hist <seed>` runs one seed. Under DEBUG, `hist_run` prints `ep / len / env_hash / map_hash` per episode, which bisects divergences to an episode. `fronts <seed>` runs the same 300 episodes and every 100 ticks measures each alive player's fronts (length, centroid spread, tile count); read-only, and its `ep` lines are `cmp`-equal to `hist`'s. It asserts its neighbour counts match `sorted_neighbors`.
- **The x86 check runs through `bundle_x86.sh` and Claude.** The bundle ships no raylib, so Claude links an 8-symbol trapping stub (`InitWindow`, `DrawRectangle`, …) that `hist_run` never calls. `-Wconversion` is checked on clang-18, because Apple clang is far quieter. `bundle_x86.sh` generates the list itself (`clang18_warnings.txt`, from the bundled committed files; needs `brew install llvm@18`): 42 at `e27e24f3`, 23 in the header. Compare warning sets keyed on message plus source text, so line shifts don't matter.
- **Hash stdout, never the binary.** ld64 randomises LC_UUID on every link.
- **Bench hygiene.** Build every binary first, then bench with nothing else running: no builds, no other sessions, and check `ps -Ao pcpu,comm -r` for background daemons pinning a core (`BTLEServer` held one at 100% during the perf pass). Interleave A/B runs and report the median of 5.
- **`./drive` must run clean after any binding change.** Its output lines are recorded in §3; compare against them.
- **zsh gotchas.** Unquoted `$VAR` doesn't word-split, so compile lines built in variables go through `bash <<'EOF'` or `${=VAR}`. `setopt interactivecomments` is in `~/.zshrc`.
- **One working tree, one session.** Keep `~/summer26/PufferLib` on `5.0`, since `mk.sh` hardcodes that path. Binpack gets its own worktree (`git worktree add ../PufferLib-binpack binpack`). `resources/constellation/experiments.ini` carries another session's uncommitted edit, so never `git add -A` in the fork.

---

## 3. What's in the header

**Signatures: read the header.** Hand-maintained copies drift, as spec §27 did. Four argument orders get transposed silently because the arguments share or convert between types:

- `heap_push(e, h, tile, pri)`: tile first, then priority.
- `conquer(e, p, t)`: player first, then tile.
- `rng_int(e, lo, hi)`: the range is `[lo, hi)`, matching upstream `nextInt` argument for argument.
- `attack_logic(...)`: a pure function. Pass it values, not `Env`.

**Sim.** The sim covers: packed terrain byte and accessors; `TileSet` with O(1) incremental border maintenance; `conquer` as the only territory mutation point; the min-heap; `Attack` with a per-attack border bitset; `attack_start` (in the order deduct → cancel → combine → find slot → assign), `attack_tick` and the pure `attack_logic`; troop growth and cap via DetMath; the dead-defender wipe; annexation (capturer by adjacency count, hole-aware largest cluster, `isEnclosed`); gold (income after troop growth, `conquer_player_gold` at the wipe and the whole-territory annex, zeroed on death); City and DefensePost structures (cost, `build_structure`, capture in `player_tick`, an end-of-tick pass for construction, marked deletion and scrap requests, the City bonus in `max_troops`, the post modifier at the attack call site, bot scrapping and the §14.2 sizing); the win check; spawn placement on the 52-tile disk; bot drivers; and xorshift32 RNG with a splitmix32 seed scramble. Player troops are `int64_t`, and every write goes through `troops_set/add/remove`, which floor. Attack troops are `double`, clamped at 0 on every write. Heap priorities stay `float` by design, since every key value is exact in float.

**All sim state lives in `struct Env`.** There are no mutable globals, and every function takes `Env *e` first. This is the precondition for PufferLib's `#pragma omp parallel for` stepping. `isolation_test` guards it: episodes run alone versus round-robin one tick at a time must hash identically. Run it after any change that touches env state.

**Load-bearing details:**

- **`alive`** is a cached `tiles.count > 0`, maintained in both directions inside `conquer`.
- **`ticks`** is zeroed at the top of `sim_reset`, before spawn placement. Otherwise spawn conquests stamp `last_tile_change` with the previous episode's counter.
- **Annexation scratch** has two separate generation counters, `cl_*` and `ff_*`, because `annex_remove` runs inside `annex_tick`'s component loop.
- **`dead_defender`** iterates the target's TileSet in descending order, because swap-and-pop would skip tiles if it went ascending.
- **One `Attack` covers one `(attacker, target)` pair** across every shared front. Combination makes that uniqueness structural.
- **`HEAPCAP` is 2048**, with a peak of ~210 since 1a. **`MAXATK` is 32**, with a peak of 7 under bots; self-play will push that up, so revisit it in Phase 3.
- **The spawn-scaled thresholds** are `WIPE_TILES` and `ANNEX_TILES`, both `SPAWN_TILES/3 = 17`.
- **The framework callocs `Env` and calls `puf_init`, never `sim_init`.** Anything filled once at init (the `lt_sig` and `cap_pow` tables) goes in `tables_init(e)`, which both call. `drive_test` asserts the tables on the `puf_init` path.

**Debug infrastructure.** Everything below sits behind `DEBUG`. `check_borders()` asserts the border invariant, phantom tiles, `alive` consistency, and that troops are non-negative and non-NaN. The test suite is `ts_test`, `conquer_test`, `blob_test`, `hole_test`, `heap_test`, `attack_test`, `isolation_test`, `annex_shore_test`, `annex_hole_test`, `gold_test`, `structure_test`, `build_test` and the `attack_logic` golden-vector test. `build_test` drives `apply_action` / `puf_step`, so it and `run_tests` sit at the end of the header, after the binding (no forward declarations). The golden vectors are 5 cases computed with libm at relative tolerance 1e-9; case 4 uses a 300k-tile defender so the territory bonus actually bites. Unit tests use a local seeded xorshift, never `rand()`. When a new invariant turns up, extend `check_borders()` first; it's cheaper than the bug.

**Binding.** The binding implements `puf_init/reset/step/log/render/close`, `compute_observations`, `sorted_neighbors`, `apply_action` and `add_log`. `ACT_SIZES {13}` (§4). `OBS_SIZE 39`: 6 self, then 5 neighbour slots × 5, then 3 self (`gold / next_city_cost`, `gold / next_post_cost`, both clipped to [0,1], and `min(cities, 4) / 4`), then 5 per-slot post flags. A build action only records a pending build on the player: `pend_type` is 0 for none, else structure type + 1 (so a calloc'd or reset `Env` has nothing pending), and `pend_target` holds the post's target player id. `post_covers(e, owner, t)` (does `owner` have a completed post with d² ≤ 36 of tile `t`) is the single check behind both the combat `has_post` argument and the obs post flag. The config keys are `num_agents`, `agent_is_bot`, `map_seed` (0 maps to 123456789) and `land_frac`.

`Log` records `perf`, `win` (land share > 0.8 at log time), `annexations` (the agent seat's own, via `annex_by[p]`), then `won / eliminated / rival_won / timeout`, then `cities_built / posts_built / build_noops`, then `n`. Like every field, the build counts are seat 0's only (the `add_log` convention); `build_noops` counts builds that did nothing, both at resolution and at issue. The framework divides each field by `n`, so the four outcome fields are fractions that sum to 1.0, which makes a free dashboard invariant. `won` (`winner == p` from `win_check`, which runs on `ticks % 10`) is not redundant with `win`; when they disagree, that's itself diagnostic. The outcome branch order is died → `winner == p` → `winner != 0` → cap, so elimination wins over `rival_won`.

**`drive_test.c` stands in for `pufferl.cu`.** It is the only thing that executes the binding path. It sets `rng = <env index>` before `puf_init`, as the framework does, and passes `land_frac = 0.65` and `map_seed = 0` from `config/openfront.ini`. It failed on the missing key from `e26522b7` (map gen, which made `puf_init` read both) until `80e82c6`. After `puf_init` it asserts `lt_sig` and `cap_pow` exactly at n = 0, 1, `OF_N/2`, `OF_N`; n = 1 is the first entry a calloc'd table gets wrong. It then drives random actions and asserts no NaNs, observations in [0,1] and episodes that terminate. When a binding invariant turns up, extend it first.

Usage is `./drive [num_agents] [agent_is_bot] [episodes]`, default `1 0 30`, one config per invocation over 4 envs. Loop over configs in bash, not zsh: zsh passes an unquoted `$a = "1 1"` as a single argument, and `atoi` silently reads only the first number.

Baseline at `e27e24f3`. All three configs changed at this commit: actions are drawn from 13 instead of 7, the obs are 39 fields, and a builds line was added. With actions restricted to 0..6 the new header reproduces the `3e26237d` baseline exactly.

`./drive` (1 agent, human seat):

```
num_agents=1 agent_is_bot=0
  episodes 120 over 4 envs, 1336 puf_steps
  mean episode length 44.3 decisions (max_steps 200)
  reward range [-1.0347, 1.2356]
  obs range    [0.0000, 1.0000]
  terminals fired 120
  action histogram: 413 413 390 423 411 411 442 395 423 412 426 397 388
  env0 log: n=30 perf=0.8542 ep_return=1.8195 ep_len=44.5 win=30 annex=1.8
  env0 builds/ep: cities=0.90 posts=4.27 noops=15.17
drive ok
```

`./drive 1 1` (the training default in `config/openfront.ini`):

```
num_agents=1 agent_is_bot=1
  episodes 120 over 4 envs, 5880 puf_steps
  mean episode length 192.6 decisions (max_steps 200)
  reward range [-1.1148, 0.0768]
  obs range    [0.0000, 1.0000]
  terminals fired 121
  action histogram: 1797 1764 1728 1831 1842 1739 1855 1878 1813 1871 1728 1811 1863
  env0 log: n=30 perf=0.0761 ep_return=-0.7920 ep_len=75.2 win=0 annex=1.2
  env0 builds/ep: cities=0.60 posts=2.93 noops=31.80
drive ok
```

`./drive 8 1 10` (8 seats, self-play shape):

```
num_agents=8 agent_is_bot=1
  episodes 40 over 4 envs, 2000 puf_steps
  mean episode length 200.0 decisions (max_steps 200)
  reward range [-1.0721, 0.0908]
  obs range    [0.0000, 1.0000]
  terminals fired 320
  action histogram: 4921 4850 4836 4939 5034 4926 4954 5045 4902 4946 4839 4834 4974
  env0 log: n=10 perf=0.0895 ep_return=-0.2453 ep_len=164.7 win=0 annex=2.2
  env0 builds/ep: cities=1.00 posts=5.30 noops=72.50
drive ok
```

**Deliberately not built:** spatial obs, a custom encoder (`openfront.cu`), `openfront_net.h`, action masking (`action_mask = NULL`, so invalid neighbour slots fall through as noop), retreat as an action, impassable terrain, boats and rivers.

**PufferLib 5.0 structure.** The env is two files, `ocean/openfront/openfront.h` and `config/openfront.ini`. There is no `binding.c` (that's 4.0) and no `<env>.c`, following upstream `7224706b`, which deleted `admiral.c`. `typedef float obs_t;` goes before `#include "pufferenv.h"`. `Log` has `perf` first and `n` last. `Env` carries the required fields `log`, `agents[]`, `tag`, `boundary_reached`, `num_agents` and `rng`. Python is gone upstream, so the env compiles into `./puffer`, run as `./puffer train|eval|match|sweep --section.key=value`. For reference envs, read `ocean/minimal/` for the smallest example and `ocean/admiral/` for a header-only one.

---

## 4. Settled decisions

Don't reopen any of these without new evidence.

- **Action space: Discrete-7** `{noop, attack TN, nb0..nb4}`, where `nbK` indexes the agent's bordering players sorted descending by shared border length. Action repeat is 10. There's no commitment head: repeating an attack supplies commitment through combination (spec §7), which makes combination a prerequisite rather than a fidelity nicety. Absolute player IDs were rejected, because they're arbitrary labels and the sorted list is permutation-invariant. The cap of 5 came from bot play, where 95.4% of late-game samples had 5 or fewer neighbours. Retreat stays out. B-lite extends this to **Discrete-13**: {noop, attack TN, nb0..nb4, build_city, post_nb0..post_nb4}. A build is recorded at issue (tick *t*) and resolved at the end of *t+1*, as upstream (spec §15.4): a City completes on *t+22*, a Post on *t+52*. Placement is automatic and integer-only.
  - **Depth** is a multi-source BFS over the player's own tiles, 4-connected. Sources are own tiles with an on-map 4-neighbour that is land not owned by the player (another player or terra nullius). Water and the map edge are not sources. Tiles no source reaches get the maximum depth, so with no sources every tile ties and the index tie-break decides.
  - **City:** the deepest placeable tile, lowest tile index on ties.
  - **post_nbK:** among own tiles at depth ≥ 2, the placeable one nearest the centroid of the own tiles bordering nbK (distinct tiles) (an int64 key), lowest index on ties. It must cover at least one own front tile (d² ≤ 36), else noop.
  - A tile blocked by min-dist falls back down the ranking.
  - post_nbK stores the neighbour's **player id** at issue, so a slot reshuffle before resolution can't retarget it. K ≥ neighbour count is a noop, counted in `build_noops`.
  - Env-side auto-targeting of posts was rejected: posts take 50 ticks, so a reactive heuristic completes after the attack lands, and choosing the front is the decision we want learned. Unaffordable or unplaceable builds are noop (no masking).
- **`apply_action` sends `troops/5` whatever `is_bot` says.** This is a deliberate divergence: upstream's Bot `attackAmount` is `/20`, but commitment size is action semantics, not economics. The five `is_bot` handicaps are `maxTroops/3`, growth ×0.5, TN loss `mag/10` vs `mag/5`, ×0.7 when a human attacks a bot, and gold income base 50 vs 100 (B-lite). That list of five is exhaustive for economics.
- **`agent_is_bot` defaults to 1**, and it's a config key. At 0, a random policy wins 23 of 25 episodes against handicapped bots (pre-terrain), so the game is solved, not learnable.
- **Agent seats:** `Agent agents[MAXP-1]` with `num_agents` from config (default 1; 8 is self-play). Seats `1..num_agents` act, and the rest run `bot_tick`. Memory is `(total_agents / num_agents) × sizeof(Env)`.
- **Death is terminal-and-idle, not respawn.** A dead seat gets `terminals = 1` once, then zeroed obs.
- **Observations: flat, `OBS_SIZE 39`** (31 before B-lite). There are 6 self features, 5 neighbour slots × 5 features, 3 B-lite self features (gold against the next City and Post cost, city count) and 5 per-slot post flags. Every field is a ratio, so obs survive rescales. A slot's post flag is measured on **q's side** of the front (q's tiles 4-adjacent to ours) via `post_covers`, exactly as combat measures a post. The known ceiling is geometry, which flat obs can't show. Don't reward annexation to compensate.
- **Reward:** the land-share delta per decision, `(tiles_now − tiles_prev) / land_tiles`, plus +1 for a win and −1 for death. The measured range is about [−1.23, +1.24].
- **Start troops are upstream's:** 25000 human, 10000 bot. The `+50000` `max_troops` floor stays unapplied. Start troops, the floor and the City troop bonus (B-lite) are one coupled group, so any rescale moves all three in one commit.
- **Policy: 2×512.** It gained +0.02 perf over 1×128 at no throughput cost. Revisit only alongside an encoder change.
- **Map:** 48×48 held. It's procedural, not real maps: shipping upstream's AGPL map assets would be a different act from deriving mechanics, and downsampling destroys the straits. The noise field never escapes the fill function. A dedicated `map_rng` keeps map gen off `e->rng`. There's one fixed map per run via `map_seed`. The land fraction is 0.65 with a quadratic edge falloff. Magnitude comes from a linear window on noise with no extra knob.
- **Terrain invariant: owned ⇒ land.** It's enforced at the only unowned-tile entry points (attack seed and refill, and the spawn disk). Any new tile-taking path must filter for land at entry. Never compare `terrain[t]` directly; use the accessors.
- **`HEAPCAP 2048`.** This is behaviour- and performance-identical to `8*N`. Revisit only if `heap_full_drops` goes nonzero.
- **Cross-platform determinism.** DetMath is ported in double (`det_exp/log/pow/pow2/atan2`), and no libm transcendentals remain in the header. `#pragma STDC FP_CONTRACT OFF` sits on the header's first line, with `DEFAULT` restored on the last, and it's load-bearing since 2a: stripping it under `-ffp-contract=fast` diverges the output. This relies on clang, which `build.sh` uses. It's deliberately not a `build.sh` flag.
- **Validation protocol.** A behaviour-changing commit needs `sweep.sh` over 20 seeds, unpaired, deciding on |Δ| < 2 SE. The runs are unpaired because trajectories diverge at episode 0. A real effect gets attributed by ablation before committing. A behaviour-neutral change needs an unchanged sha, nothing less. When a commit necessarily grows `Env`, behaviour-neutral means stdout identical except the `sizeof` line and added test lines, with every `ep` line identical; if the commit also extends `env_hash` to cover new state, the `env` field may change but every length and map hash must match. Used for `7b4e6d01` and `61a71536` (both extended `env_hash`) and `e27e24f3` (every `ep` line identical). Five seeds underestimate sd and produced one false positive.
- **x86 check trigger:** run it for any commit touching FP, libm/DetMath, type widths or format strings, and always before a training run or an upstream push. Otherwise batch it.
- **Scope:** the full game, staged by tier (spec §0). **PR #1 is Tier A plus Tier B-lite**, meaning gold income, the `conquerPlayer` gold transfer, City and Defense Post, and bot structure scrapping. Upgrades out of PR #1 (a second City is the same decision as an upgrade: same cost counter, same bonus). Without gold and structures, the agent has one decision. Factories are deferred: they're all of Tier F, and degenerate at 48×48 because station range 110 exceeds the map. Nukes are the likely second PR. Spatial obs, a conv encoder and a tile-targeted head are the first follow-up after PR #1, judged by elimination rate.
- **Tier A trims:** spawn phase is equivalent by construction; spawn immunity goes to Phase 3 (inert at `agent_is_bot = 1`); river-crossing `nearby()` goes to Tier C; fallout exclusion goes to Tier D.
- **Recorded divergences.** These are kept, not fixed:
  - The per-attack RNG: upstream seeds every attack with 123.
  - Iteration order: JS `Set` insertion order versus `TileSet` order, in annexation cluster formation and the dead-defender pass.
  - `MAXATK` 32 versus upstream's unbounded list: the no-slot path refunds, and has never fired.
  - `troops/5` for the agent.
  - Bot scrap order: `bot_delete_next` takes the first unmarked structure in **structure slot order**; upstream takes `units()` order, which may differ after captures.
- **Known limitation, heap cap vs border set.** If a `heap_push` were ever refused, the border set would count a tile the heap lacks. `heap drops` is 0 in every run so far.
- **Bot stalls are upstream behaviour.** The reserve gate keys on `max_troops`, not current troops. They're kept, which means the eval bots are weaker than they look.
- **Licensing is settled.** Mechanics and constants aren't copyrightable, and a header comment crediting OpenFront.io is the mitigation. Derive, don't transliterate.
- **Nobody else in the Puffer Discord is building this env.** `djmango/openfront-ai` wraps the TS engine and doesn't conflict.

### Rescales applied for 48×48

Every applied rescale gets a row here.

| Item | Upstream | Ours | Why |
|---|---|---|---|
| Wipe threshold | 100 tiles | `SPAWN_TILES/3` = 17 | 100 of ~500k tiles vs 2% of our land; anchored to spawn size. Elimination is insensitive to it (thresholds 8–42 move it by 2 points). |
| Annex always-check threshold | same | 17 | same constant |
| Spawn min-distance | 30 Manhattan | 13 Manhattan, still dropped after 750 of 1000 attempts as upstream | 30 exceeds most of a 48×48 map |
| Sigmoid debuffs | present at `fc50009` | removed | upstream removed them at `7defd24` anyway |
| `max_troops` floor `+50000` | present | **not applied** | coupled to start troops and City bonus |
| TN cost clamp | present | not applied | the lower clamp always binds |
| goldMultiplier | 1 | 10 | first City at ~6–13% of episode vs upstream ~5–10%; upstream-native config knob, costs stay verbatim |
| City troop bonus | 250k/level | 25k/level | keeps bonus/cap at T≈100–500 equal to upstream's at T≈3k–30k (+79%..+30% vs +73%..+23%); coupled with the maxTroops floor and start troops |
| Defense post range | 30 | 6 | radius 7–8 covers 64–83% of median territory (242 tiles); at ×5 mag one post would shield nearly everything. 6 covers ~47% of median land, ~half the longest front (`fronts` p50 ext 9.8). Tune after retrain if posts are ignored or dominant. |
| Structure min-dist | 15 | 3 | upstream ratio range/min-dist = 2 |

---

## 5. Open items

### 5.1 Perf pass (done)

Perf pass done (`fa64934c`, `3e26237d`). Remaining headroom: the growth `det_pow(troops, 0.73)`, once per player per tick, which can't be tabled.

### 5.2 Tier B-lite (code complete at `e27e24f3`)

All five decisions were answered 25 Sept (§4 and the rescale table). The code is in `7b4e6d01` (gold), `61a71536` (structures) and `e27e24f3` (build actions, placement, obs, log).

**`fronts` measurement (25 Sept, harness at `3e26237d` header).** `./of_dbg fronts <seed>`, 300 episodes × 2000 ticks, sampled every 100 ticks over alive players. `len_e` is `sorted_neighbors`' shared count, which counts (own tile, neighbour tile) adjacent pairs, so a tile touching two of q's tiles counts twice; `len_t` counts distinct own tiles. The neighbour sets are identical. The longest front is `sorted_neighbors`' slot 0. Centroid and `ext` (max Euclidean distance from the centroid to a front tile) are over distinct tiles. Nearest-rank percentiles, p25 / p50 / p75 / p90:

| Stat | seed 42 | seed 43 |
|---|---|---|
| longest `len_e` | 23 / 31 / 43 / 59 | 23 / 31 / 43 / 59 |
| longest `len_t` | 17 / 23 / 31 / 42 | 17 / 22 / 31 / 42 |
| longest `ext` | 7.57 / 9.84 / 13.17 / 17.55 | 7.56 / 9.82 / 13.18 / 17.24 |
| all `len_e` | 11 / 19 / 29 / 42 | 11 / 19 / 28 / 42 |
| all `len_t` | 8 / 14 / 21 / 30 | 8 / 14 / 21 / 30 |
| all `ext` | 3.88 / 6.46 / 9.49 / 13.27 | 3.89 / 6.42 / 9.47 / 13.35 |
| tiles | 139 / 242 / 385 / 582 | 141 / 241 / 382 / 571 |
| fronts per player | 2 / 3 / 4 / 5 | 2 / 3 / 4 / 5 |

Samples: seed 42 has 29483 player-samples, 90676 fronts, 931 beyond slot 5; seed 43 has 29767, 91804 and 894. Every alive sample had at least one front. These numbers set range 6 and min-dist 3 (rescale table).

**Implementation notes.** `conquerPlayer`'s gold transfer runs at both call sites: the dead-defender wipe and `annex_remove` on a whole-territory take. Posts don't shoot at this anchor. The two distance tests differ, as upstream: post range is inclusive, d² ≤ 6² (`post_covers`), and min-dist is strict, d² < 3² (`can_place`). **Bots use `expandRatio`, not `reserve`, against a Bot that owns structures** (spec §14.2); this applies to the agent at `agent_is_bot = 1`. It's expected, not a bug.

**Retrain watch-list.**

- `cities_built` / `posts_built`: whether the policy spams posts or invests in Cities.
- The `build_noops` trend.
- Episode length and timeouts. Under a random policy, `./drive 8 1 10` now hits the 200-step cap in every episode (186.2 before). The cause is not settled. Single 40-episode random runs at `e27e24f3`, one action seed, not significance-tested:

  | Actions | Mean length |
  |---|---|
  | 7 (old space) | 186.2, the old baseline exactly |
  | 13, builds mapped to noop (attack 6/13 instead of 6/7) | 197.4 |
  | 13, City builds only | 189.1 |
  | 13, Post builds only | 200.0 |
  | 13, all builds | 200.0 |

  Action dilution accounts for most of the shift, but posts alone reach the cap, so post strength is not ruled out.

### 5.3 Side items

**Open code checks:**

- **`float lf = dict_get(...)` in `puf_init`** (`openfront.h:3526` at `e27e24f3`, confirmed under clang-18). This is the only double→float narrowing on clang-18. `land_frac` feeds map gen through `puf_init`, which the harness never exercises, so a fix could change the training maps without moving the harness sha. It needs its own check.
- **`bundle_x86.sh` generates the warning list; the raylib stub is still missing.** It needs `Color`, `KEY_ESCAPE` and 8 functions: `InitWindow`, `IsWindowReady`, `SetTargetFPS`, `IsKeyDown`, `BeginDrawing`, `EndDrawing`, `ClearBackground`, `DrawRectangle`.

**Upstream PRs:**

- **Two build-flag PRs are unopened.** One is bash-3 `build.sh` (`1c37a9a3` on `fix-build-sh-macos-bash3`); the other is `ccache` (`command -v ccache >/dev/null && CCACHE=ccache || CCACHE=""`). Open them as separate PRs. They're the fastest route to merged upstream commits. Verify the commit email matches a verified GitHub email first.

**Before the PR:**

- **Clause-by-clause audit.** Map each numbered spec rule to the line that implements it. Three of the ten Aug re-audit defects were rules the spec had right and the code missed.
- **`SKILL_ISSUES.md` conformance pass** once the header stops moving. The doctrine covers inlining single-use functions, asserts over defensive checks, no forward declarations, 80/100 columns, 4-space indents and no file splits. New code is written to it already.
- **Re-merge upstream `5.0`.** Upstream force-pushes, so don't pin work to its hashes. Procedure: diff `src/pufferenv.h src/puffercpu.c build.sh` first; after merging, rerun `drive` and require bit-identical output.
- **Read** PufferLib's "common bugs if your env is not training" checklist, and `jordanbailey00/fc-rl` (PufferLib PR #8, a contributor env in the same position) for PR scoping.

**Deferred experiments and refactors:**

- **A `max_steps` experiment**, with its own control.
- **Re-run the neighbour histogram against a trained policy.** A policy that sprawls thin shifts it right, which would test the cap of 5.
- **Collapse `pos[]` into global `tile_pos` / `border_pos`.** That takes 144 B/tile to ~80. It's a standalone refactor that blocks nothing.

### 5.4 Phase plan

| Phase | State |
|---|---|
| 0 | Throughput prototype. Done. |
| 1 | Action space, mechanics, `Env` refactor and binding. Done; the clause-by-clause audit (§5.3) is still owed. |
| 2 | **Underway.** Training works. Tier A done. Now: perf → B-lite → retrain → draft PR. |
| 3 | Self-play. Scripted bots become held-out eval opponents via `PUF_HAS_BOT_POLICY` / `puf_set_bot_policy(Env*, int)`, which upstream already ships. Note that `src/pufferl.cu:1850` asserts the GPU env backend doesn't support selfplay or `match`; OpenFront is a CPU env, so it isn't blocked. |
| 4 | Sweep, polish, PR. |

---

## 6. Training

### 6.1 Results so far (all pre-Tier-A)

**17 Sept, open grid, 1×128, 500M.** Perf (land share) reached 0.331 and win 0.086, against a random baseline of 0.004 and 0. Entropy settled at 1.244 with `clipfrac` and `kl` at 0. That's clean convergence, not collapse. Win outpaced perf (5.4× vs 1.5×) while annexations stayed flat, so the shaping handed off to the objective as designed. Diagnosis: a capability ceiling.

**21 Sept, terrain, 100M each:**

| policy | seed | perf | win | eliminated | timeout | rival_won | annex/ep |
|---|---|---|---|---|---|---|---|
| 1×128 | 73 | 0.323 | 0.244 | 0.460 | 0.287 | 0.009 | 2.18 |
| 1×128 | 74 | 0.336 | 0.260 | 0.449 | 0.278 | 0.013 | 2.21 |
| 2×512 | 73 | 0.354 | 0.285 | 0.435 | 0.267 | 0.012 | 2.23 |
| 2×512 | 74 | 0.342 | 0.277 | 0.448 | 0.262 | 0.013 | 2.20 |

**Reading of the terrain runs:**

- **Terrain didn't move the ceiling.** 1×128 perf was 0.330 vs 0.315 on the open grid.
- **The win rate tripled mechanically**, because the 0.8 bar fell.
- **Capacity helped, but only slightly** (+0.02).
- **Elimination sits at ~45% regardless of policy.** That's the number spatial obs has to move.
- **Seed variance is about ±0.013 perf at 100M.** Differences under ~0.03 are noise.

Checkpoints are on the Mac at `~/summer26/ckpts/step7/run{0..4}_*.bin`: run0 is the 10M smoke run, run1 1×128 s73, run2 2×512 s73, run3 1×128 s74 and run4 2×512 s74. To eval 1×128 files, pass `--policy.hidden_size=128 --policy.num_layers=1`.

### 6.2 Reading runs

- **Sanity-check epoch 1 against the random baseline.** At `agent_is_bot = 1`, a random policy dies around decision 55 with perf ≈ 0.004 and entropy just under ln 7 = 1.946. A suspiciously good curve means a config key defaulted. This check is also what proves the ini is well-formed.
- **Read centres, not samples.** Dashboard `win` swings in a ±0.04 band from epoch to epoch. Two wrong calls have already been made off single samples. Compare centres across large step gaps.
- **Don't report win rate as a headline.** It's cap-limited, since most games hit the 2000-tick cap. Perf (land share at a fixed horizon) is the eval metric, and "beat the scripted bots" is retired as a milestone.

**Random-policy reference**, measured through the binding with 4 envs. It's pre-terrain and pre-Tier-A, so it's qualitative only:

| config | land share | wins | seat-0 lifespan | return |
|---|---|---|---|---|
| `num_agents=1`, `agent_is_bot=0` | 0.835 | 32/32 | 39.8 | +1.81 |
| `num_agents=1`, `agent_is_bot=1` | 0.004 | 0/30 | 59.0 | −0.99 |
| `num_agents=8`, `agent_is_bot=1` | 0.150 | 0/20 | 150.9 | −0.32 |

### 6.3 Trainer operations (5.0)

- **Config:** `config/openfront.ini` is read, and `[env]` keys reach `puf_init`. Set `device = cuda`, which is committed; with `cpu` the GPU sat at 3%.
- **Checkpoints:** `checkpoint_dir` and `checkpoint_interval` live in `[base]`. The interval counts **epochs**, not steps: 100M ≈ 1525 epochs. The final epoch always saves. Weights are flat fp32 `.bin` files.
- **Eval is a Mac job.** The Mac `--cpu` binary loads and renders Vast `.bin` checkpoints (tested 21 Sept) with `./openfront <ckpt.bin>`, run from the repo root. The binary reads `config/openfront.ini` from the cwd, so the policy shape must match the checkpoint. `--headless --eval_episodes=N` prints metrics. In the render, the agent is seat 1 in red. `./puffer eval` segfaults when headless on Vast, and under `xvfb-run` it prints nothing. Don't spend instance time on it.
- **Throughput:** ~290K SPS measured pre-Tier-A. The sim bench is now ~830k vs ~750k+ then (different workloads), so re-measure SPS on the retrain rather than projecting. The dashboard `SPS` reads high; use steps ÷ uptime. The env is 81–95% of the loop and the GPU idles, so if learning stalls the fix is reward, obs or capacity, never throughput.
- **Memory:** `pufferl.cu` allocates `(total_agents / num_agents) × sizeof(Env)`. `sizeof(Env)` is 1,022,240 bytes. The 20 Sept decomposition (at 938 KB) was ~179 B per scaling tile plus ~525 KB fixed (32 `Attack` slots, each with a heap); 4b's scratch arrays and 1a's border bitsets have added to both since. Larger grids are reachable without a refactor: 128×128 is ~3.5 GB of host RAM at 1024 agents, with step time ~7× slower. 48×48 is a held experimental constant, not a memory wall. The honest answer to "why 48×48" on stream is that the territory representation is O(P·N) and hasn't been refactored yet. Upstream's smallest shipped map is ~350×350.

---

## 7. Working conventions

**The split.** Claude writes the simulation code and Sam audits it. Binding and framework work is done together. The audit is the load-bearing half: Joseph reviews PRs on stream, and anything Sam can't explain unprompted isn't done. Two things worth being able to explain cold:

- **Why annexation's cluster fill runs over the border set, not the territory.** A territory with a hole is one territory but two border components, which is exactly the distinction the rule needs. It's also why the cost is O(border).
- **Why the enemy bounding box must contain the cluster's box**, not the reverse. The reverse reading can never fire.

**When Sam says "just give me the code" or "I'm confused",** don't paste a body. Trace one tile or one number through the operation concretely. Handing over more finished code faster is what broke the split the first time.

**Hand over whole functions, never excerpts.** Excerpt boundaries are where adjacent required lines get dropped: one excerpted handover produced three clean-compiling defects, including a completely dead sim and doubled attack costs. The same applies to these docs: replace whole sections.

**Mechanical multi-site edits go to Claude Code**, through scripts that assert every anchor matches exactly once. A hand-applied partial migration of the terrain byte once made water read as land. When migrating an encoding, enumerate the writes as well as the reads.

**Claude verifies handed-over code** compiles and passes before handing it over.

**Read the signature, not the call site.** Same-type argument confusion produced two spec defects (`inscribed(outer, inner)`, `nextInt(min, max)`). If a rule you've transcribed turns out to be unfireable, the transcription is probably wrong.

**Recurring C bug classes:** `=` vs `==`, `.` vs `->`, struct by value vs pointer, missing braces, missing return, nested function definitions, integer division, `continue` in a nested loop, and C99 VLAs (not C++). The compiler catches none of the following: **tile vs player confusion** (tiles are `t`, `nb[k]` and heap contents; players are `p`, `attacker`, `target` and `owner[...]`, and all of them are bare ints), and transposed arguments in the four signatures listed in §3.

**Code that is never executed is not verified.** When you add an interface, add the thing that exercises it in the same pass.

**Don't reason about cache from `sizeof`.** Untouched `calloc` pages never fault in, so measure the touched footprint.

**One variable per experiment.** Don't run phases ahead of where the code is.

---

## 8. Resume / PR claim discipline

Claim only what is built and shipped. No feature lists; mention only mechanics that change the RL problem.

**True now:**

- Tier A conformance to upstream `7defd24` for the territorial core, backed by a golden-vector combat test and a scenario test (`annex_hole_test`) that fails on the pre-fix code. Say "reimplemented from the source's mechanics."
- Output byte-identical across arm64 and x86_64, with hashed baselines gating each behavioural change.
- O(1) incremental border maintenance, as an original design decision.

**True but stale; refresh after the retrain and the perf pass:**

- The ~35% average land share at 100M (2×512: 0.354 / 0.342), against random play at 0.4%. It's "nearly 3× an even split": by symmetry, the bots' average can't exceed 12.5%, and the agent plays with the same handicaps.
- The throughput figure: ~830k ticks/sec on the current header (M3 Pro, single core); re-measure after B-lite before quoting.

**Never write:**

- **Any speedup ratio against `djmango/openfront-ai`.** The comparison isn't controlled for map size, mechanics, obs or hardware. The honest version is a design rationale: that project wraps the TS engine at ~2,100 ticks/sec, which is why a native C sim was worth building.
- **"Validated against the original."** No differential test exists.
- **Win rate.**

**Not yet true:** maintainer review, an open or merged PR, "contributed", or anything about structures or gold. Fork commits don't reach the contribution graph; only merged upstream PRs do.

**Resume draft** (24 Sept, adoption unconfirmed; bullet 2's throughput number is stale):

```
OpenFront RL Environment – PufferLib | C	May 2026 – Present
Built an 8-player territory-conquest environment in C for training reinforcement learning agents
Reimplemented the game's core mechanics from the original source, running at ~1M simulation steps per second
Designed observation spaces and reward structures; trained agents to ~35% average land share in 8-player games, nearly 3× an even split
Wrote a test suite verifying the simulation's mechanics and producing identical results across platforms
```

After B-lite, add "and structure economy" to bullet 1. If the retrained agent demonstrably builds Cities and Posts under pressure, that result replaces the land-share line.
