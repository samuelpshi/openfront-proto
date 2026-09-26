# OpenFront Env — History

Archive. Nothing here is current; `openfront_project_reference.md` is. Not loaded into chats.

Entries are appended here when the reference is updated in place. The first entry is the full reference as it stood before the 25 Sept 2026 restructure, kept verbatim so nothing was lost in the cut: session narratives, superseded baselines and the baseline history table, the Phase 0 / phase1-baseline throughput record, the Aug re-audit defect table and punch list, the Env-refactor control table, the cross-platform determinism investigation, the three versions of the random-policy table, the hand-merge incident, and the 4.0 → 5.0 migration notes.

---

## Archived: project reference as of 25 Sept 2026 (pre-restructure)

## OpenFront Env — Project Reference

Companion to `openfront_env_spec.md`. That file is the **mechanics** reference (formulas, constants, source-derived rules). This file is the **project** reference: state, tooling, audit findings, decisions, and working conventions. When they disagree on mechanics, the spec wins.

Last updated: **25 Sept 2026 — Tier A complete** (`4a3d2848`, verified Mac↔x86). Real terrain is in, and 100M on terrain shows the plateau is an **observation** ceiling, not terrain and not capacity (§6.5). Policy baseline is 2×512. The Mac evaluates and renders Vast checkpoints. Env is header-only at `ocean/openfront/openfront.h` + `config/openfront.ini` on `samuelpshi/PufferLib` branch `5.0`. Local training is still impossible. **The header now conforms to upstream `7defd24` for the Tier A scope (spec §25); nothing trained yet on the Tier A build.** Next: perf pass (throughput fell from 1.02M to ~525k ticks/sec across Tier A, §1), then Tier B-lite open decisions (§5), then B-lite, then the baseline retrain. PR #1 stops at Tier A plus gold, City and Defense Post (spec §0). Harness output is byte-identical across Mac arm64 and x86_64 (§5).

---

### 1. Status

**Phase 0 complete. Phase 1 complete except the audit. Phase 2 underway — the env trains end to end.**

**Session of 25 Sept 2026 — Tier A complete.** Commits on `5.0`, oldest first: `84ebc825` #2b integer troops; `5175a70e` 52-tile spawn disk; `908b785a` #1a attack border set; `c4faca4a` #1b combat formulas; `9b542489` #4a annex capturer; `d6aef90c` #4b hole-aware selection; `4a3d2848` header anchor `fc50009` → `7defd24`. Dev repo: `harness.c` prints `ep / len / env_hash / map_hash` per `hist_run` episode (DEBUG only) and has a `hist <seed>` mode; new `sweep.sh` (§2).

**History was rewritten once.** `5.0` was `filter-branch`ed (author-email fix) before this session: `1f3eae15` → `eea12848` (clamp), `9f303628` → `39db150f` (2a); `297cad49` kept its hash. Trees are identical. This file and the spec use the new hashes throughout.

- **#2b, `int64_t` player troops.** Every write goes through `troops_set` / `troops_add` / `troops_remove` (upstream `setTroops` / `addTroops` / `removeTroops`, all flooring; negative add → remove, so decay truncates toward zero). 20-seed sweep vs 2a: every metric |t| ≤ 0.8, i.e. no detectable aggregate effect. A 5-seed read first showed tiles moved −1435 at "t ≈ −2.6"; three single-floor ablations (defender loss, growth, attack deduction/refunds) each sat with pre, and n=20 dissolved it. That was the lesson that fixed the protocol (§5).
- **Spawn disk (§25 item 7).** Upstream's `(dx+0.5)² + (dy+0.5)² ≤ 16`, written as `(2dx+1)² + (2dy+1)² ≤ 64` (52 tiles, offsets −4..3). `SPAWN_TILES` 49 → 52 moves `WIPE_TILES`/`ANNEX_TILES` 16 → 17 through `/3`. Sweep: annexations −156 (t −5.1), others flat. Ablation with thresholds pinned at 16: −117 (t −3.5) — the disk, not the threshold.
- **#1a, attack frontier.** The frontier was `heap.count`, which counts duplicate enqueues — measured **1.91×** the deduplicated set. Now a per-attack bitset (`border[OF_N/64]`, `border_size`): add on enqueue, remove on pop *before* validity checks, cleared at attack start — upstream `AttackImpl` border set. `borderSize == 0` (0.024% of attack ticks) takes exactly one valid tile, as upstream's divide-by-zero → `Infinity` does. Attacks roughly halved in speed: tiles moved −16% (t −14), wins −10.5 (t −3.9), annexations −226 (t −9.9).
- **#1b, combat formulas.** Pure `attack_logic()` mirrors `Config.attackLogic`: `tickBudget = 1`, per-tile `tickFraction`, ratio-scaled density loss, the new speed curve, log-logistic territory bonuses via DetMath (≈ 1 − 1.2e-6 at our scale), `has_post` hook for B-lite. DEBUG golden-vector test (5 cases from libm, rel tol 1e-9; case 4 uses a 300k-tile defender so the bonus actually bites). Sweep vs 1a: wins −9.4 (t −3.5), annexations +72 (t 2.4), tiles +794 (t 1.9). Zeroing all bonuses leaves seed-42 output bit-identical, so the movement is the density term and speed curve. **Cost: −9% bench, all from the three bonus evaluations per vs-player tile.**
- **#4a, annex capturer.** Two gaps §25 didn't list: upstream `getMode` counts every adjacency (ours counted once per tile per owner), and ties go to first-encountered owner (ours: lowest id; largest-attack scan: slot order). Sweep: all |t| ≤ 1.4.
- **#4b, hole-aware largest cluster.** A cluster whose bbox is contained by another cluster in the same 8-connected component of own tiles is a hole rim; pick the largest non-hole. Upstream's lazy territory ids overwrite on re-flood, but each flood labels a whole component, so the test reduces exactly to component membership — any correct implementation matches. `annex_hole_test` (377-tile rim vs 124-tile perimeter, two enclosing enemies) **fails on 4a with P wiped and passes on 4b**. 20-seed sweep bit-identical: the case is unreached in bot games. `sizeof(Env)` 957704 → 985352 (scratch arrays).
- **Items closed without code:** spawn phase (equivalent by construction — nothing runs in it, and all spawns are placed at reset), spawn immunity (inert at `agent_is_bot = 1`; Phase 3), river-crossing `nearby()` (coupled to boats; Tier C), `#9` dead-defender wipe (conformant; friendliness vs target inert, `conquerPlayer` gold live with B-lite). Full list in spec §25.
- **Throughput.** Bench ~583k ticks/sec before 1b, ~525–535k after. The quoted 1.02M (`phase1-baseline`) is pre-Tier-A; the drop happened across DetMath / 2a / 2b / earlier commits and is unattributed. Perf pass next: bisect `of_fast bench` over the Tier A commits, then the LTB lookup table (`s[n]` per `Env`, bit-identical by construction).

**Session of 22 Sept 2026 — Tier A #2a and a live bug.** Commits on `5.0`: `297cad49` (unit tests use a local xorshift instead of libc `rand()`), `eea12848` (attack-troops clamp + DEBUG invariant), `39db150f` (Tier A #2a, float → double sim math). Dev repo: `-Wfloat-conversion -Wimplicit-float-conversion` added to `mk.sh` and `cxxcheck.sh`.

- **Negative attack troops were live.** Spec §7.3 clamps attack troops at 0 on every write; the C sim didn't. `attack_tick`'s loop checks `troops < 1` only at the top, so the last iteration could leave an attack negative, where it stayed until its next tick. In between, `attack_start`'s cancel and combine read it (combination shrinks a new attack; cancellation's `>` test inflates it). A temporary probe counted **~11k clamp hits per 300-episode `hist_run`** (5 in `attack_test`), yet harness output was byte-identical with the clamp — no cancel/combine read a negative attack in bot-only play. **Training runs before `eea12848` may have hit it**, since the agent sends attacks far more often than bots. Found by the new attack-troops invariant during 2a.
- **2a verification** (precision changes can't be bit-identical, so this replaces the one-variable control): lockstep trace of episode 0 against the float build at `eea12848` — no tile or attack-set divergence through tick 1200; max relative troop diff 3.8e-6 (players) and 4.2e-5 (attacks, normalised to start troops). The +2 `TRACE START` events in the double build mean the *event stream* diverged by then, even though snapshot state did not. `attack_start`'s **merge** path never fired in the window, so it has no lockstep coverage. Multi-seed (seeds 42–46, 300 eps each): wins float 83.2 ± 6.8 vs double 75.2 ± 6.5, sign flips across seeds, paired t ≈ −1.3 — noise.
- **Tooling findings.** Apple clang's `-Wfloat-conversion` does not flag double→float; `-Wimplicit-float-conversion` does. On clang-18 the only double→float narrowing in the header is `float lf = dict_get(...)` in `puf_init` (out of scope; cleanup commit). Baseline hashes are of `of_dbg` stdout, never the binary (ld64 randomises LC_UUID per link).

**Session of 21 Sept 2026 — map-gen plan steps 3–7 closed.** Commits on `5.0`: `e26522b7` map gen (step 3), `81b078ab` the `map_seed` / `land_frac` ini keys that step 3 missed, `1874b8ff` shore split (step 4), `48754b77` dead-guard removal (step 6), `6941ce5` `device = cuda` in the ini (message says "inherit"; the change sets `cuda` explicitly — ignore the message), then the 2×512 policy commit. Proto repo: `3d38294` `spawn_sweep`, plus `cxxcheck.sh` finally committed.

- **Step 3, map gen.** Vendored `simplex.h` (from `ocean/battle/`), octave wrapper minus its VLA, dedicated `map_rng` + `map_seed` / `land_frac` keys (`map_seed = 0` maps to 123456789 in `puf_init`), quadratic edge falloff, quantile threshold at 0.65, ocean flood fill, land magnitude from noise (clamped 0–30), water magnitude `min(31, ceil(dist/2))`, shoreline bits, spawn restricted to the largest land component. Islands and lakes kept. **Land is pinned at exactly 1498 tiles** (`2304 − (int)(0.35 × 2304)`). 200 maps: Plains 61.4 / Highland 32.1 / Mountain 6.5, 1.2 land components, 5.2 lakes.
- **Step 4, shore split — behaviour-neutral by construction.** `annex_surrounded`'s largest path gates on `is_ocean_shore` (`PlayerExecution.ts:374`), others on `is_shore` (`:432`). On the largest path the gate is redundant: any lake-shore tile has an unowned water 4-neighbour and the largest path bails on `owner == 0` — upstream does the same (`if (ownerId === 0) return false`). Kept for source fidelity. The non-largest path *skips* unowned neighbours, which is why it needs the stricter gate. The first `annex_shore_test` passed on terrain map gen cannot produce (shore bit with no water neighbour); rewritten to real geometry only.
- **Step 5, spawn sweep — no change.** 1000 map seeds × 8 players: 0 hard failures, 6.9% of maps relax min-distance once, max depth 1. Largest component 1491–1498. The sweep's spawn loop is a copy, verified tile-identical to `sim_reset` over all 8000 slots.
- **Step 6, terrain audit — invariant: owned ⇒ land, by induction.** The only unowned-tile entries are the attack seed (~715) and refill (~777), both `is_land`-filtered, and the spawn disk (`spawn_disk_ok`: bounds, land, unowned; same disk as the conquer loop). Every other `conquer` caller moves owned tiles. `conquer`'s water check is a DEBUG trap only. All territory denominators use `land_tiles`; no `OF_N` denominator outside tests. `has_tn_neighbor` is land-gated and matches the seed predicate, so bot TN attacks are never empty; `bordering_players` excludes 0. Empty agent attacks refund on `heap_pop == -1`; with an empty heap the budget is 0 whenever `rng_int(0, 5)` draws 0, so the refund can lag a tick. Two dead `is_land` guards removed (`atk_push`, post-pop in `attack_tick`). No raw `terrain[t]` comparison survives in the sim path.
- **Step 7:** see §6.5.

**Current `hist_run(300, 2000, 42)` baseline — the acceptance target for any behaviour-neutral change** (Tier A complete, `4a3d2848`, 25 Sept; byte-identical on Mac arm64 and x86_64 under both x86 builds, no masking; stdout includes the per-episode `ep … env … map …` lines):

```
wins 54 (18.0%), mean length 1920, eliminated 64.2%
annexations 3857 (12.86/ep), tiles moved 29773 (7.7/event)
spawn failures 0, heap peak 209/2048, heap drops 0
sizeof(Env) = 985352
of_dbg stdout sha256 bce152dee0f18893e17e7b06ac7e0635ad94b318d1e99663debb30156f1d2c0d
```

History (`hist_run` seed 42; stdout sha256 prefix):

| After | wins | len | elim | annex | tiles moved | heap | sha |
|---|---|---|---|---|---|---|---|
| #4a capturer | 54 (18.0%) | 1920 | 64.2% | 3857 | 29773 | 209 | `46c1fa05` |
| #1b combat | 61 (20.3%) | 1930 | 62.9% | 4139 | 30181 | 211 | `cb396ce3` |
| #1a border set | 52 (17.3%) | 1945 | 65.0% | 3766 | 29293 | 205 | `8695b6ae` |
| spawn disk | 96 (32.0%) | 1878 | 67.2% | 3977 | 32007 | 263 | `03af9c44` |
| #2b `int64_t` | 92 (30.7%) | 1889 | 68.0% | 4229 | 33863 | 292 | `bca45248` |
| harness env_hash lines (2a sim) | 69 (23.0%) | 1911 | 66.3% | 4233 | 35092 | 284 | `1ac326ac` |
| #2a double (`39db150f`) | 69 (23.0%) | 1911 | 66.3% | 4233 | 35092 | 284 | `795fe2a3` |
| #1 + clamp (`eea12848`) | 87 (29.0%) | 1882 | 66.8% | 4159 | 32660 | 287 | `deb2937e` |

Single-seed wins swing by ±9 with nothing changed (seed sd ≈ 9 over 20 seeds) — read changes off the 20-seed sweep, never this table. Terrain step 3, pre-Tier-A — wins 74 (24.7%), mean length 1905, eliminated 66.8%, annexations 4317 (14.39/ep), tiles moved 34503 (8.0/event).

Bots resolve 24.7% of games (was 5.0%), mostly because the 0.8 land-share bar fell from ~1693 to ~1198 tiles with the 2000-tick budget fixed. **Do not compare win rates across the terrain change.** The blocks below are history.

First real training run, 17 Sept 2026, Vast RTX 3090, `samuelpshi/PufferLib` @ 5.0. Full results in §6. Headline: 500M steps, ~30 min, random → 8.6% win rate against eight scripted bots. The learning curve is real and the failure mode is a capability ceiling, not a bug.

**Session of 20 Sept 2026 — two commits landed, both verified.**

1. **Episode outcome logging.** `Log` gained `won` / `eliminated` / `rival_won` / `timeout` between `annexations` and `n`, with matching `dict_set` in `puf_log`. `add_log` now takes `winner`. The framework casts `Log` to a flat float array and divides every field by `n` (`src/puffercpu.c:953-962`), so the four read as fractions of episodes and **sum to 1.0** — a free dashboard invariant. Branch order is died → `winner == p` → `winner != 0` → cap; elimination deliberately wins over `rival_won` when the winning attack is what killed the agent. Note `won` is not redundant with the existing `win`: `win` is a land-share > 0.8 test at log time, `won` is `winner == p` from `win_check`, which only runs on `ticks % 10`. Disagreement between them is itself diagnostic.

2. **Packed terrain byte** (see §5). Pure encoding change, verified **bit-identical** against the pre-change build — the one-variable control that §7 demands. `env_hash` was switched to hash the *derived* tier (`is_land ? terrain_type + 1 : 0`) so the control stayed comparable across the encoding swap.

**Cost of the partial migration, recorded so it is not repeated.** The first attempt applied the struct and accessor changes but not the ten call-site substitutions. Because water now carries the ocean bit (`0x20`), every surviving `terrain[t] == 0` became false for water and **water read as land** — heap peak 350 → 437, annexations 5831 → 7103, wins 15 → 42. Worse than a no-op. The four test-suite `memset(e->terrain, 1, ...)` calls had the mirror-image bug (byte 1 is now *water*) and masked it by staying consistent with the unmigrated comparisons. Lesson: enumerate terrain **writes** as well as reads, and mechanical multi-site substitutions belong in Claude Code, not hand-applied.

**`hist_run(300, 2000, 42)` baseline — pre-terrain, blob generator. SUPERSEDED 21 Sept — history only.**

```
wins 15 (5.0%), mean length 1988 ticks, eliminated 53.6%
annexations 5831 (19.44/ep), tiles moved 71444 (12.3/event)
spawn failures 0, heap peak 350/2048, heap drops 0
sizeof(Env) = 938664
```

**The win metric is cap-limited, and this reframes it.** `hist_run`'s 2000 ticks equals training's `max_steps 200 x action_repeat 10`, so the two are directly comparable. Only **5% of episodes resolve**; the other 95% hit the cap, and the 15 that did resolve took ~1760 ticks on average. So `win` is not measuring "did the agent win" — it measures "did anyone reach 0.8 land share inside 2000 ticks," and eight scripted bots manage that 5% of the time. **The trained agent's 0.086 is already above the bot baseline.** `max_steps` is therefore a larger lever on that metric than terrain is, but it must be its own experiment with its own control — changing episode length and terrain together destroys the comparison against 0.086.

**Prediction recorded before the fact:** terrain will probably push resolution *lower*, since coastline and chokepoints slow expansion and conquest. If `win` drops after map gen, that is not evidence against terrain. Read `perf`, elimination rate, and annexations instead.

**Phase 0 detail below is history.** It settled the go/no-go and nothing since depends on it.

---

`proto.c` — the original standalone single file, since split into `openfront.h` (sim + binding) and `harness.c` (the statistics/bench main). It existed to answer one question: ticks/sec of the territory loop.

| | |
|---|---|
| Result | **868,577 ticks/sec** (median of 3: 870,341 / 868,577 / 867,313 — 0.35% spread) |
| Conditions | 2M ticks, 2000 episodes, ~2.30 s, M3 Pro, `-O2` |
| Baseline commit | `samuelpshi/openfront-proto`, tag `phase0-baseline` |
| Gate | ≥500k → build full env; ≤100k → fall back; between → profile |
| Container cross-check | 411k on the same code (Mac ≈ 2× a shared container core) |
| **Superseded by** | **`phase1-baseline`, 1,020,828 ticks/sec — see below** |
| Prior art | `djmango/openfront-ai` wrapped the real TS engine: ~2,100 game-ticks/sec after heavy optimization |

**What the number actually measures — read before quoting it.** 868k ticks/sec is the territory/attack/economy loop on a 48×48 map (2,304 tiles, 2,116 land) with 8 scripted bots. It is *not* a like-for-like benchmark against `openfront-ai`, and the ratio should not be stated as "400× faster." Four confounds:

1. **Map size.** 48×48 vs. real OpenFront maps, which are the 10^5–10^6-tile maps the economy constants were tuned for (the reason §14 of the spec exists). djmango's map size is unknown — don't assume it.
2. **Missing mechanics.** No annexation, dead-defender wipe, real spawn placement, cancellation/combination, or bot handicaps at benchmark time. His tick is a full game tick; ours was territory + combat only. **Superseded by the full-game scope decision (§5): naval, nukes, trade, alliances, cities/gold, structures and Nation AI are staged tiers (B–H, `openfront_env_spec.md` §0), not cut.**
3. **No observation encoding.** His figure comes from a system that needed a learned spatial autoencoder to compress obs — per-step work proportional to map size. Ours has none yet.
4. **Unknown hardware and measurement conditions on his side.**

The 2,100 figure remains legitimate as *motivation* — it's why writing a native C sim beat wrapping the TS engine — but it is context, not a result.

**Expected trajectory.** The missing mechanics are mostly cheap: annexation is deliberately O(border) with the `lastTileChange` skip, the wipe is bursty but rare, cities/gold are per-player scalars. **Obs encoding is what will actually cost**, and it lands in Phase 2. Re-measure then rather than carrying 868k forward.

**Measurement hygiene: done.** Three runs, 0.35% spread, median taken. Committed and tagged `phase0-baseline` in `samuelpshi/openfront-proto` (standalone repo, deliberately *not* the PufferLib fork — the fork's diff against upstream should be only `ocean/openfront/` + config when the PR opens, and non-fork repos count toward the contribution graph).

**ANCHOR SUPERSEDED (Aug 2026). `phase0-baseline` is no longer a valid comparison point and 868k is no longer the quotable number.** The current shipping figure is **1,020,828 ticks/sec** (M3 Pro, `-O2`, 2M ticks / 2000 episodes, 48×48, 8 scripted bots, all seats `is_bot = 1`), tagged `phase1-baseline`.

The tag cannot be used as a control because the code has diverged in eight mechanics, not one. `phase0-baseline` predates `is_bot` entirely (no bot handicaps — every seat had human economics), annexation, cancellation, combination, the dead-defender wipe, spec §12 spawn placement, the elimination flag, and the xorshift32 RNG migration. Reconstructing its config means reverting all eight, at which point it is a different sim being called a control. Two attempts to build a defensible A-config against it were abandoned for this reason; don't spend a third.

**Note the direction, because it is the interesting part.** The tag ran *less* work per tick than current code (no annexation, which costs 13%; start troops 1000, which idled bots until ~tick 106 of every 1000-tick bench episode) and still measured lower. So the 868k → 1.02M gap is not game content getting cheaper. It is unattributed, and chasing it further is not worth the time — it blocks nothing.

**STALE as of Tier A (25 Sept 2026): the current build benches ~525k ticks/sec, not 1.02M.** Do not quote 1.02M against the current header. The perf pass bisects where it went; the 1b share (−9%, territory-bonus evaluation) is already attributed.

Headroom still on the table (not worth touching yet): `det_pow` twice per player per tick (a `log` + `exp` series each — slower than `powf`, unmeasured), `max_troops` recomputed up to 3× per bot decision, no cache work.

---

### 2. Build & tooling

**Two repos, one canonical header (Sept 2026).** `openfront.h` lives in the fork at `~/summer26/PufferLib/ocean/openfront/openfront.h` and nowhere else. The dev tools — `harness.c`, `drive_test.c`, `mk.sh` — stay in `~/summer26/openfront` (repo `samuelpshi/openfront-proto`) and reach the header through an `-I` path. Both `.c` files keep a bare `#include "openfront.h"`.

The single-copy rule is the point: a duplicated header drifts, and then the file that gets audited is not the file that trains. That is the worst available bug in this project. If a second copy ever appears, delete it rather than syncing it.

**`~/summer26/openfront/mk.sh`** — the dev loop. Builds all three binaries:

```bash
#!/bin/bash
set -e
P=$HOME/summer26/PufferLib
INC="-I $P/raylib-5.5_macos/include -I $P/src -I $P/vendor -I $P/ocean/openfront"
RL=$P/raylib-5.5_macos/lib/libraylib.a
FW="-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL"
SAN="-fsanitize=address,undefined"
gcc -g -O0 -Wall -Wextra -DDEBUG $SAN $INC harness.c    -o of_dbg  $RL $FW -lm
gcc -g -O0 -Wall -Wextra         $SAN $INC drive_test.c -o drive   $RL $FW -lm
gcc -O2 -Wall -Wextra                 $INC harness.c    -o of_fast $RL $FW -lm
```

`of_dbg` = tests + `check_borders()` + `hist_run` statistics. `of_fast` = bench. `drive` = the binding driver. Note the header now needs the fork present to compile at all — it includes `pufferenv.h` and links the fork's static raylib for `puf_render`. That coupling predates the move; the move only made it explicit.

**Through the framework** (from `~/summer26/PufferLib`):

```bash
./build.sh openfront --cpu     # -> ./openfront, standalone eval main + raylib render
./build.sh openfront           # -> ./puffer, the trainer. REQUIRES nvcc. Not on Mac.
```

- **`-lm` is required** (`floor`, `sqrtf`; `powf` is gone since Tier A commit 1). Missing it is a link error, not a compile error.
- **`mk.sh` passes `-ffp-contract=off`; `build.sh` doesn't and mustn't.** In the framework build, the header's own `#pragma STDC FP_CONTRACT OFF` is the mechanism (see §5). `build.sh` still needs `bash ./build.sh` on the Mac until the bash-3 PR lands — `/bin/bash` 3.2 fails on `${ENV^^}`.
- **`-DDEBUG` is load-bearing.** Without it `run_tests()` compiles to an empty stub and the program runs silently — looks like a pass. VS Code's default clang task lacks it. Use `mk.sh` or fix `tasks.json`. This cost real debugging time once already.
- Clean under `-Wall -Wextra` in both modes, which is **stricter than upstream** — `build.sh`'s `CLANG_WARN` is `-Wall` plus three targeted `-Werror=` promotions (`incompatible-pointer-types`, `return-type`) and no `-Wextra`. Keep the stricter bar; just don't expect upstream's build to catch what `mk.sh` catches. The `-Wunused-parameter` noise from upstream's own `puf_set_bot_policy` stub is a `-Wextra` artifact and never fires on the shipped path.
- **The header is compiled as C locally and as C++ on the training path, and `mk.sh` cannot see the C++ side.** `nvcc` compiles `openfront.h` as C++; `gcc` in `mk.sh` compiles it as C. Two defects of this exact class landed on the same day (macro collisions, `_Static_assert`), both invisible locally and both discovered one nvcc error at a time on a paid instance. Local proxy, now in `~/summer26/openfront/cxxcheck.sh`:

```bash
#!/bin/bash
g++ -fsyntax-only -x c++ -std=c++17 \
  -I "$HOME/summer26/PufferLib/src" \
  -I "$HOME/summer26/PufferLib/vendor" \
  -I "$HOME/summer26/PufferLib/raylib-5.5_macos/include" \
  "$HOME/summer26/PufferLib/ocean/openfront/openfront.h" && echo "c++ ok"
```

  Run it with `mk.sh` after any header change; both must pass before pushing. Committed to `openfront-proto` 21 Sept (it had been untracked). It emits one harmless `#pragma once in main file` warning — artifact of compiling the header directly, not a problem. **What it proves:** language-level C/C++ incompatibilities, which is the class both defects belonged to. **What it does not prove:** link errors, nvcc-specific diagnostics, or *macro collisions* — those come from upstream's files including yours, and this compiles yours alone. Strong filter, not a guarantee. Expected future offenders: designated initializers, implicit `void*` casts, enum arithmetic.
- **`sweep.sh <dirA> <dirB>` (dev repo) — the acceptance test for any behaviour-changing commit.** Builds each header dir at `-O2`, runs `hist <seed>` for seeds 42–61 (300 episodes each), prints mean A, mean B, Δ, SE and t for wins / annexations / tiles moved. Build the "before" dir with `git show HEAD:ocean/openfront/{openfront,simplex}.h`. Running a dir against itself gives Δ = 0 everywhere — a free null control.
- **x86 check (`bundle_x86.sh` → Claude).** The bundle ships no raylib; Claude links an 8-symbol trapping stub (`InitWindow`, `DrawRectangle`, …) that `hist_run` never calls. Worth folding into the script. **`-Wconversion` is checked on clang-18, not the Mac:** Apple clang 15 reports 6 warnings where clang-18 reports 36 (it groups `-Wimplicit-int-float-conversion` under `-Wimplicit-float-conversion`). Compare warning sets keyed on message + source text so line shifts don't matter.
- **zsh gotchas.** Unquoted `$VAR` does not word-split (compile lines built in variables need `bash <<'EOF'` or `${=VAR}`), and `#` isn't a comment interactively without `setopt interactivecomments` (now in `~/.zshrc`).
- **One working tree, one session.** On 25 Sept a Claude Code session found the fork checked out on `binpack` by another session. Put binpack in its own worktree (`git worktree add ../PufferLib-binpack binpack`) and keep `~/summer26/PufferLib` on `5.0` — `mk.sh` hardcodes that path. `resources/constellation/experiments.ini` carries another session's uncommitted edit; never `git add -A` in the fork.
- **`ccache` is a hard dependency of the `native` (trainer) build** — `build.sh` invokes it unconditionally with no fallback, so a machine without it dies at `line 506: ccache: command not found` before the compiler runs. `brew install ccache` locally; check the image on Vast.

---

### 3. What's implemented in `openfront.h`

Done and verified: coordinate layer, terrain gen, `TileSet` (swap-and-pop, O(1)), `Player` struct with `tiles`/`border` sets, `conquer` with O(1) incremental border maintenance, binary min-heap, `Attack` struct, `attack_start`, `atk_push` with the real priority formula, real budget/combat math, `attack_tick` drain loop, `player_tick` troop growth, `bots_init`, `has_tn_neighbor`, `bordering_players`, `bot_tick`, `sim_reset`/`sim_run`/`win_check`/`bench`, xorshift32 RNG, spec §12 spawn placement, attack cancellation/combination, dead-defender wipe, elimination flag.

Test suite (behind `DEBUG`): `ts_test`, `conquer_test`, `blob_test` (12×12 solid rects — catches a missing interior `ts_remove` in `update_border`), `hole_test` (3×3 bite — catches a missing neighbor-update loop in `conquer`), `heap_test`, `attack_test`. Plus `check_borders()` — full border-invariant + phantom-tile scan, called every tick in debug.

Exact signatures live in `openfront_env_spec.md` §15. Two that get transposed: `heap_push(Heap*, int tile, float pri)` — **tile first** — and `conquer(int p, int t)` — **player first**. Both are ints (or silently convert), so the compiler catches neither.

**Env-struct refactor: done (Aug 2026).** Every simulation global — map, `owner`, `players`, `is_bot`, `attacks`, `bots`, `spawn_center`, `last_calc`/`last_tile_change`, `ticks`, `land_tiles`, the RNG state, both annexation scratch blocks with their generation counters, and all instrumentation counters — is now a field of `struct Env`, and every sim function takes `Env *e` first. `nm` on the release binary shows no mutable globals; the only remaining file-statics are `test_set`/`test_present` inside `#ifdef DEBUG`.

This was the precondition for PufferLib, which steps envs under `#pragma omp parallel for` (`src/vecenv.h`, `src/pufferl.cu`): a shared scratch array or a shared generation counter corrupts nondeterministically and only under parallel vec, which is the worst possible place to find it.

- `sim_run` split into `sim_tick(e)` (one tick, returns winner or 0) and `sim_run(e, n)`. Action repeat needs per-tick granularity, and the isolation test needs it to interleave.
- `rng_seed` applies a splitmix32 finalizer before coercing zero. `pufferl.cu` assigns `env->rng = <env index>` *before* calling `puf_init`, so seat 0 would receive 0 — xorshift32's fixed point — and low seats would get adjacent states with correlated early output.
- **`HEAPCAP` `8*N` → 2048.** `sizeof(Env)` 4.89 MB → 0.89 MB. PufferLib `calloc`s one `Env` per agent slot up front, so `8*N` would have asked for ~10 GB at 2048 agents. Verified behaviour-neutral: A/B against a `HEAPCAP (8*N)` build is byte-identical on every episode statistic, because peak occupancy is 440 over 3000 episodes and zero pushes are ever refused. Also verified performance-neutral (0.12% on the M3, inside run-to-run spread) — allocation size is not working set; only ~8 attack slots are ever active and the touched footprint is ~24 KB either way. New counters `heap_peak` / `heap_full_drops` make a future cap hit loud instead of a silently truncated frontier. **440 is a scripted-bot floor, not a bound** — a policy that sprawls thin holds longer frontiers, so re-read `heap_peak` in Phase 2.

**The refactor was verified behaviour-neutral in isolation, which is the part that matters.** Three changes landed in the same pass (globals→struct, the seeding change, start troops), so a control was built with the latter two reverted and `HEAPCAP` restored — one variable. It is **bit-identical** to the pre-refactor build on all five reported statistics (7 wins, 1996 mean ticks, 47.8% eliminated, 5368 annexations, 63793 tiles moved). Layering the changes back attributes the movement exactly:

| | wins | mean length | eliminated |
|---|---|---|---|
| refactor only (control) | 2.3% | 1996 | 47.8% |
| + splitmix32 seed | 2.7% | 1998 | 48.5% |
| + source start troops | 9.0% | 1973 | 53.7% |
| + `HEAPCAP` 2048 (shipping) | 9.0% | 1973 | 53.7% |

Note what this rules out and what it doesn't. A *stale global reference* was never reachable — the globals were deleted, not shadowed, so `players[p].troops` is now a compile error rather than a silent read. The reachable failure was a transposed argument or wrong player index, which compiles clean and passes `check_borders()`; the bit-identical control is what excludes it.

**New test: `isolation_test`.** Three envs run three full episodes each alone and hashed, then the same seeds rerun round-robin one tick at a time — the access pattern the vec loop produces. Bit-identical. Any surviving shared array, counter, or generation stamp diverges the hashes, and nothing else in the suite catches that class. Run it after any change that touches env state.

**Sequencing rule that worked and should continue:** placeholder economics before real economics. Flat budget/cost/loss until territory visibly spreads, *then* swap in the real formula. Everything else implemented verbatim from spec constants first, so any weird balance is attributable to the constants rather than to the code.

---

### 4. Audit findings (300 instrumented episodes)

**Clean:** no water tiles conquered at spawn; heap never reaches `HEAPCAP`; attack slots never exhaust (32 is plenty); no player goes negative on troops; combat never creates troops from nothing across 400 consecutive ticks. `land_tiles` = 2116 of 2304.

**Open gaps** — Phase 1 punch list. Items 1–4 are **done** (Aug 2026), verified by re-running the instrumented 300 episodes:

| | before | after |
|---|---|---|
| mutual attacks | 3,671 | 0 |
| duplicate `(attacker, target)` | 207 | 0 |
| dead-but-`alive` player-ticks | 785,399 | 0 |
| wipes fired | 0 | 1,682 |

1. ~~Elimination flag~~ — one line at the end of `conquer` clearing `alive` when the previous owner hits 0 tiles. Throughput went *up* (`player_tick`/`bot_tick` stop running on dead players).
2. ~~Attack cancellation~~ (spec §5.4).
3. ~~Attack combination~~ (spec §5.5). Was a prerequisite: the action space has no troop-commitment head because repetition supplies commitment via this mechanic.
4. ~~`attack_start` silent slot drop~~ — troop deduction moved ahead of cancellation per §5, which would have leaked troops on slot exhaustion, so the no-slot path now refunds. Deliberate divergence: source has an unbounded attack list. Never fired; peak usage 7 of 32 slots.

**Notes from those changes, worth keeping:**
- In `attack_start`, order is load-bearing: deduct → cancel → combine → find slot → assign `a->troops`. Both loops mutate the local `troops`, and combination can free a slot.
- `troops <= 0` early return in the cancellation loop is not cosmetic — without it a later mutual match does `attacks[i].troops -= troops` with negative `troops`, which *adds* troops to the enemy attack.
- `dead_defender` iterates the target's TileSet **descending**. `ts_remove` swaps the last element into the freed slot; descending means that element is already visited. Ascending skips unvisited tiles.
- **One `Attack` = one `(attacker, target)` pair spanning every shared front.** The seed loop walks the attacker's whole border set. Multi-front is not multiple attacks. Combination now makes that uniqueness structural.
- `MAXATK` slack: theoretical ceiling is 8 players × (neighbors + TN) ≈ 32, i.e. exactly `MAXATK`. Observed peak is 7 because bots decide every 40–80 ticks. Self-play at action-repeat 10 will push this up — revisit in Phase 3. Cost of raising it is cache, not RAM (~147KB/slot, mostly the embedded `Heap`).

**Spawn placement + RNG: done (Aug 2026).** Spec §12 BFS disk replaced the hardcoded 5×5 grid.

- `SPAWN_RADIUS 4` → 49-tile Euclidean disk (matches source's ~49). Radius 3 would be 29 tiles.
- `SPAWN_MIN_DIST 13` Manhattan between centers, **dropped after 750 of 1000 attempts** — min-distance is a preference, not a constraint, so a cramped map degrades to packed spawns instead of hanging. Source does the same.
- Filter order is cheap-first: center unowned land → center touches nobody (source's border-tile rejection; for an unowned tile "is border" means any 4-neighbor is owned at all) → min-distance → full 49-tile disk validity. The disk check is 81 candidate cells, so it runs last.
- Failure path: 1000 attempts exhausted → player doesn't spawn, gets `alive = 0`, `spawn_failures` counts it. Never fired in 300 episodes. Infinite retry would hang a training run silently; returning without marking dead would recreate the zero-tile-but-alive bug.
- **`fill_terrain` now runs per episode**, so mountains vary too, not just spawns. More diversity than §12 asks for and correct for training — but the map is no longer constant across runs, so debugging a specific board means seeding and re-deriving it.
- Result: 0 spawn failures / 300 episodes, ASan+UBSan clean, spawn centers differ every episode, neighbor histogram unchanged (cum at 5 = 95.9%, mean 3.00) so the action-space cap of 5 still holds.

**RNG migrated off `rand()`.** xorshift32 (13, 17, 5), single 32-bit state word, one full cycle of 2³²−1 over the nonzero states; `rng_seed` coerces 0 because 0 is a fixed point. Chosen for cost, not quality — three shifts and three xors, no multiply, no memory traffic — since this is jitter for map blobs and bot personalities, not Monte Carlo. Every sim-path call site moved; tests still use `rand()`, which is fine. *(Superseded: tests moved to a local seeded xorshift in `297cad49`, §5.)* **Why it had to happen now:** PufferLib runs thousands of envs in parallel and they cannot share a global stream, and a half-migration leaves episodes correlated. Swapping `rng_state` for a field on the env struct in Phase 1 is then trivial.

- `rng_below(n)` has modulo bias ~n/2³² — about 10⁻⁸ for n ≤ 48, invisible here, but the failure mode exists for large n.
- `rng_int(lo, hi)` is **`[lo, hi)` — min inclusive, max EXCLUSIVE**, matching source's `PseudoRandom.nextInt(lo, hi)` (`PseudoRandom.ts` computes `floor(next()*(hi-lo)) + lo`). That is why the priority formula reads `rng_int(0, 7)` rather than the equivalent `rng_below(8)`: keeping the two-argument form means **every `nextInt(a, b)` in the TS transcribes to `rng_int(a, b)` argument-for-argument**, and no argument is ever adjusted while transcribing. *(This bullet previously claimed "inclusive both ends," which contradicted defect 3 in the table below and spec §1 item 8. Corrected Aug 2026.)*

**`WIPE_TILES` is now `SPAWN_TILES / 3` = 16.** Spec §14's `landTiles/50` (=42) fires after a player loses only 7 of its 49 starting tiles, which is not what the rule means: source's threshold is 100 of ~500k tiles (0.02% of the map) whereas 42/2116 is 2%. Anchored to spawn size instead. **Measured across 300 episodes, the constant barely matters** — thresholds of 8 / 16 / 24 / 42 give elimination rates of 70.5% / 71.3% / 71.8% / 72.6%. Once a player is collapsing it crosses the whole range in a few ticks. 16 is preferred over 42 only because it has a reason attached.

**Phase 1 punch list: DONE (Aug 2026).** Bot economy handicaps, annexation, and the retaliation branch all landed, plus a full re-audit of every mechanic against source commit `fc50009`.

#### Defects found in the re-audit

| | what | root cause |
|---|---|---|
| 1 | Annexation bbox test inverted (spec §9) | same-type argument transposition reading `inscribed(outer, inner)` at the call site |
| 2 | `isEnclosed` gate missing entirely | spec had no version anchor; source grew the gate after the spec was written |
| 3 | `rng_int` inclusive, source `nextInt` exclusive | assumed convention, asserted confidently in spec §6 |
| 4 | Bot terra-nullius loss used `mag/5`, source `mag/10` | spec §7 said it; code missed it |
| 5 | `conquer` accepted water | spec §4 said "we guard or assert"; code missed it |
| 6 | Bot TN branch consumed the decision tick | spec §11 said it falls through; code missed it |
| 7 | Start troops 1000 with the 50k floor left as written | half-applied a coupled §14 rescale |
| 8 | `alive` maintained in one direction only | local invariant, never written down |
| 9 | `ticks` not zeroed before spawn placement | latent until `conquer` started stamping `last_tile_change` |
| 10 | Defender troops unclamped below zero | `powf(negative, 0.73)` → NaN, one sign error from reachable |

**Three of these — 4, 5, 6 — are things the spec got right and the code missed.** The spec is not the weak link; the missing piece is anything that checks spec clauses actually landed. Before the PR, do a clause-by-clause pass mapping each numbered spec rule to the line that implements it.

**Items 8–10 surfaced the moment the audit harness could express them and not before.** `check_borders()` now also asserts `alive == (tiles.count > 0)`, non-negative troops, and non-NaN troops, every tick in debug builds. Extend that harness first whenever a new invariant is discovered — it is cheaper than the bug.

#### §14 rescales: applied vs not

| §14 item | status |
|---|---|
| Wipe threshold → `SPAWN_TILES/3` = 16 | **applied** |
| Annex always-check threshold → same constant | **applied** |
| Economy floor `+50000` | **not applied** (as written) |
| Start troops | **restored to source (25000 human / 10000 bot)** — coupled to the floor, which stays unapplied |
| TN cost clamp | not applied (as written; lower clamp always binds) |
| Sigmoid debuffs | **dropped entirely** per §14.5 — the `defDebuff`/`sigmoid` terms are absent from the code, not present-as-written. *(Row previously read "not applied (as written)", which implied the formula existed. Corrected after the Claude Code audit.)* |
| Spawn radius / min-distance | applied (radius 4, min-dist 13) |

**The start-troops finding: RESOLVED (Aug 2026) — restored to source.** Start troops had been cut ~10–25× while the 50k `max_troops` floor was left alone. The bot expand/reserve gates key off `max_troops`, which the floor dominates at 48×48, so bots spawned unable to afford their first attack: mean tick of the first attack anywhere was **106** at 1000 start troops versus **7** at source's 10000/25000. That was not a balance preference but an accidental state produced by moving one half of a coupled pair, and the current state was the one indefensible option.

Now `START_TROOPS_HUMAN 25000` / `START_TROOPS_BOT 10000`, read through `start_troops(e, p)` off `is_bot`, so flipping the agent seat picks up the right value with no second edit. **The +50000 floor stays unapplied, as written.** The two move together or not at all; any future rescale changes both in one commit and updates the table above.

**Every balance number recorded in this file after Aug 2026 is keyed to source start troops.** Older figures measured at 1000 are not comparable and are labelled where they appear.

#### Bot handicaps: measured effect (300 episodes each)

**Measured at start troops 1000 — superseded config, kept for the shape of the effect only.** The relative ordering holds; the absolute numbers do not transfer.

| config | wins | mean length | eliminated |
|---|---|---|---|
| neither handicap | 88.0% | 1373 | 71.2% |
| annexation only | 90.3% | 1305 | 70.6% |
| `maxTroops/3` only | 58.0% | 1689 | 69.4% |
| growth ×0.5 only | 19.7% | 1939 | 59.9% |
| both | 1.0% | 1999 | 47.5% |

**Current shipping config (both handicaps, source start troops, 300 episodes): 9.0% wins, mean 1973 ticks, 53.7% eliminated.**

The handicaps compound hard and essentially stop the bot-only game terminating; annexation is behaviourally near-free. Restoring source start troops recovers some termination (1.0% → 9.0% wins) but does not fix it — episodes still run the full 2000-tick limit ~91% of the time, which is a **reward-shaping problem for Phase 2**: at action-repeat 10 the agent gets ~200 decisions in an episode that rarely reaches a terminal.

#### "Beats the scripted bots" is not saturated — it is solved

Running the **identical scripted policy** on seat 1 with `is_bot[1] = 0` (human economics) against seven handicapped bots: **300/300 first place, 86.9% mean land share, mean 638 ticks to the 80% win condition.** No learning involved. Under source start troops it is 294/300 and 85.7%.

So if the agent seat gets human economics, win rate against these bots measures the handicap, not the policy.

**Stronger result, measured through the binding (Aug 2026, re-measured after the co-driving defect below): a UNIFORMLY RANDOM policy alone on the agent seat with `agent_is_bot = 0` wins 23 of 25 episodes at 0.766 mean land share, in a mean 41.8 decisions.** Not a scripted policy — random action sampling from the Discrete-7 space. Source economics against seven handicapped bots does not produce a saturated metric, it produces a solved game. There is nothing for a policy to learn, and any training curve under this config measures the handicap arriving on schedule.

*(First measurement of this — 28/29 at 0.804 — was confounded: `sim_tick` ran `bot_tick` on the agent seat too, so a scripted bot co-drove it. Caught in the Claude Code audit, fixed, re-measured. The conclusion survived the correction; the confound was worth ~0.04 land share.)*

Same measurement at `agent_is_bot = 1`, corrected: **0.000 mean land share — the random agent dies at a mean decision 54.7 of 200**, zero wins, return −1.02. The earlier 0.026 figure was the stowaway bot keeping the seat alive. The true random baseline is death before mid-episode, which strengthens the case that this config has learnable headroom: survival alone moves land share off zero.

**`is_bot[agent]`: RESOLVED (Aug 2026) — config key `agent_is_bot`, default 1.** Read in `puf_init` via `dict_get`, so it stays a sweepable curriculum knob rather than a hardcoded choice, and `is_bot[]` is per-seat so the four economy handicaps compose with it unchanged. Default 1 because 0 is unlearnable *by being trivial*, which is worse than unlearnable by being hard — a dense reward with room to move beats a terminal that arrives regardless of play.

The earlier recommendation in this file was default `0`, argued from terminal signal. That argument was wrong and the random-policy measurement is why. The eval metric still has to change — land share at fixed horizon, ticks-to-80%, or margin over the best bot — because the scripted bots are weak regardless (see bot stalls below).

#### Annexation cost

13% throughput with economics held constant (454k → 387k ticks/sec in-container). The first implementation cost 20%; `neighbors8` written as a dx/dy double loop calling `ref(nx, ny)` was 9% of total throughput on its own, recovered by offset arithmetic. The `isEnclosed` fill is measurement noise despite walking unclaimed land.

**Do not quote the full-build before/after number.** It shows throughput *rising* 445k → 521k, but that is the handicapped game doing less work per tick, not the code getting faster. Same confound as the djmango comparison (§1). ~~Re-measure on the M3 against tag `phase0-baseline`.~~ **Done and abandoned — the tag is not a valid control (§1). The current anchor is `phase1-baseline`, 1,020,828 ticks/sec on the M3.**

**Bot stalls — not a bug, but load-bearing for Phase 1 reward/episode design.** A representative 300-episode trace shows territory completely frozen from ~tick 175 to ~250 (five of eight players static for 75 consecutive ticks), and one player sitting at its starting 25 tiles until tick 275 before finishing at 279. Cause: reserve ratio gates on `max_troops` (capacity), not current troops, so a bot below its own threshold does nothing until it rebuilds. Source behavior, kept deliberately. Consequences: long flat stretches make reward hard to learn from at action-repeat 10, episode outcomes are partly determined by the RNG on per-bot sampled constants rather than by play, and the agent (which has no reserve gate) will be strictly more active than the scripted opponents — so the held-out eval bots are weaker than they look.



---

### 5. Decisions made and settled

- **Action space: settled (Aug 2026).** Discrete 7: `{noop, attack TN, nb0, nb1, nb2, nb3, nb4}`, where `nbK` indexes into the agent's bordering players **sorted descending by shared border length**. Action repeat 10 (one agent decision per 10 sim ticks). Fixed troop fraction per attack (`troops / 5`, spec §5 default) — no commitment head.

  Rationale. Absolute player IDs were rejected: IDs are arbitrary labels with no stable meaning across episodes or seats, so a fixed `{p1..p8}` space makes the agent learn a symmetry that shouldn't exist, and most indices are invalid most of the time. The relative, border-sorted list is permutation-invariant and makes index *k* mean a consistent thing.

  Cap of 5 chosen from instrumentation (300 episodes, sampled every 10 ticks, 145k late-game samples with ≥80% of land claimed): distribution of distinct bordering players was 1:10.5%, 2:26.8%, 3:28.6%, 4:19.8%, 5:9.7%, 6:3.7%, 7:0.9%; mean 3.06. Cum at 5 = 95.4%. The dropped tail is cheap because the list is sorted by shared border length and frontier is exactly what the budget formula scales on (§7) — a neighbor touched along 3 tiles is barely attackable anyway. Extra indices cost exploration for near-worthless arms.

  Troop commitment is deliberately absent because repetition supplies it: spec §5 step 5 (combination) makes a second outgoing attack on the same target absorb the first. **This makes combination a prerequisite, not a fidelity nicety** — see §4.

  **`apply_action` sends `troops/5` regardless of `is_bot` — deliberate divergence, flagged in the Claude Code audit.** Source's `attackAmount` default is `/20` for a Bot attacker, so under the shipping `agent_is_bot = 1` a fifth `is_bot`-reading rule exists in source and is not implemented. Kept at `/5` because commitment size is action semantics rather than economics — halving the agent's only commitment lever while combination already supplies scaling would shrink agency for fidelity nobody measures. The "four handicaps read `is_bot`" list is economics-only and stays exhaustive for economics.

  Retreat (spec §13) stays out of v1; one more index, easy to add once it's known whether the agent loses attacks it should abandon.

  **Caveat:** the histogram is from scripted-bot play. A trained policy that sprawls thin will touch more neighbors and shift the distribution right. Re-run the instrumentation against a trained policy in Phase 2 before treating the cap as final.

- **Agent seats: `Agent agents[MAXP-1]`, `num_agents` from config, default 1 (Aug 2026).** Eight slots are declared regardless; `Agent` is six pointers and an int, so the max costs ~400 bytes in a 0.89 MB struct. Seats `1..num_agents` take policy actions, the rest run `bot_tick`. `num_agents = 1` is the Phase 2 config and `8` is Phase 3 self-play, with no header rewrite between them — and `num_agents` is genuinely runtime, since `pufferl.cu` calls `puf_init` and *then* reads `env->num_agents` to decide how many envs to create.

  Memory follows from it: `num_envs = total_agents / num_agents`, so at `num_agents = 1` host RAM is `total_agents × 0.89 MB` (0.9 GB at 1024, 1.8 GB at 2048). At 8 seats it is an eighth of that, and every sim tick yields eight agent-steps instead of one.

- **Death is terminal-and-idle, not respawn (Aug 2026).** A seat that reaches zero tiles gets `terminals[0] = 1` once, then idles with a zeroed observation until the episode ends. Faithful to source, which has no respawn. Measured cost at 8 seats: seat 0 averages 126.4 of 200 decisions before dying, so ~37% of its steps are post-terminal idle. Respawn would have to beat that number to justify the divergence; it is an experiment, not a default.

- **Observations: flat feature vector, no spatial map, `OBS_SIZE 31` (Aug 2026).** Six self features (troops/capacity, land share, frontier ratio, TN adjacency, neighbour count, episode progress) plus five slots × five features (valid, share of contested frontier, their land share, log troop ratio, incoming attack). Every field is a ratio — nothing divides by a raw economy constant, so the obs survives a §14 rescale and is invariant to map size.

  Rationale: the action space is seven options, and choosing among them needs far less than "understand the board." What flat obs cannot express is geometry — an agent cannot see that it is three tiles from closing a ring, so it can only annex by accident. Annexation is a side mechanic in practice, so v1 accepts that ceiling and measures it: `Log.annexations` counts the agent seat's own annexations (`annex_by[p]`, not everyone's). If a trained policy sits at the accidental baseline, the spatial obs plus a custom encoder becomes a Phase 2 experiment with a number to beat rather than an upfront assumption.

  **Do not reward annexation to compensate.** A shaped bonus for a mechanic the obs cannot perceive teaches the agent to chase a signal it has no mechanism to control. It moves land share when it happens, which is already the reward.

- **Reward: land-share delta + terminal (Aug 2026).** `(tiles_now − tiles_prev) / land_tiles` per decision, `+1` on win, `−1` on death. Three components. The delta form is deliberate — absolute land share as a per-step reward reads 0.020 then 0.021, no magnitude change to differentiate, the same failure as negative-distance-to-target. Delta also makes territory loss negative for free, so no separate defence term is needed. Summed deltas reach ~0.8 over an episode, comparable to the terminal bonus, so neither dominates. Measured reward range across all configs: [−1.23, +1.24].

- **Start troops: source values, ratified (Aug 2026).** `START_TROOPS_HUMAN 25000` / `START_TROOPS_BOT 10000`, with the `+50000` `max_troops` floor left unapplied as written. This was previously carried as an open decision; it is now the standing choice and every balance figure recorded after Aug 2026 is keyed to it. The pair is coupled — a future rescale moves the floor and both start values in one commit, or not at all. Rationale in §4.

- **Policy: 2×512, settled (21 Sept 2026).** +0.02 perf over 1×128 across two seeds at no throughput cost (§6.5). Revisit only alongside an encoder change.

- **Terrain invariant: owned ⇒ land (21 Sept 2026).** Enforced at the only unowned-tile entry points (attack seed/refill, spawn disk); `conquer` checks water only under DEBUG. Any new tile-taking path must filter land at entry or the invariant breaks. Never compare `terrain[t]` directly — use the accessors.

- **Spawn constants unchanged after the terrain sweep (21 Sept 2026).** Land is pinned at 1498 by the quantile threshold, so land area can't stress spawning. Revisit only if `land_frac` or map size changes.

- **Heap capacity: `HEAPCAP 2048`, settled.** Behaviour-identical and performance-identical to `8*N` at 5.5× less memory (§3). Nothing to trade off; do not revisit unless `heap_full_drops` becomes nonzero, which is what it exists to tell you.

- **Map size: hold 48×48 through Phase 1.** Sim speed doesn't bind (64×64 would be ~1.78× the tiles, ~490k ticks/sec, still fine). Observation size binds — map-proportional per-agent per-step work is what forced a learned autoencoder in the prior art. If Phase 2 obs design goes egocentric + downsampled, the underlying map size becomes nearly free and can be revisited then. `W`/`H` stay compile-time defines; the static arrays are part of why it's fast. If the map ever feels cramped, dropping to 6 players is cheaper than growing it.

- **Cities and gold: deferred to Phase 2, revisited at action-space design.** Gold alone is bookkeeping — a number that goes up. Cities are the real thing: build-vs-expand is the primary tradeoff that makes agent policy interesting, and without it always-expand is close to dominant. Deferred anyway because cities don't move the ticks/sec number Phase 0 existed to measure, they're not a one-evening build (placement rules, cost curves, the `+ sum(cityLevels)*250000` term in `maxTroops`, obs encoding), and Phase 0 was two functions from done. *(Superseded 24 Sept 2026 by the PR #1 stopping point below: gold, City and Defense Post are in PR #1.)*
- **Licensing: settled, do not relitigate.** OpenFrontIO is AGPL-3.0. Rules, mechanics, and constants are not copyrightable expression; a C reimplementation from the spec does not trigger it. Only a line-by-line transliteration would be derivative. A header comment crediting OpenFront.io is the entire expected mitigation.
- **Sam does not read the TypeScript and does not need to.** `openfront_env_spec.md` is the interface to the source. If something looks off, re-clone and re-read the named file — don't reconstruct from memory.
- **Cross-platform determinism (Tier A commit 1, 21 Sept).** Upstream's `DetMath.ts` is ported to C in double as `det_exp/det_log/det_pow/det_pow2/det_atan2` (only `+ − × /`, `floor` and IEEE bit views via `memcpy`, so correctly rounded everywhere). All libm transcendentals are gone from the header: `powf` ×2 (troop cap, growth) and `log2f` ×1 (obs troop ratio — obs drive the policy, so they're on the closed-loop determinism path; written as `det_log(x)/DET_LN2`, no `det_log2`, so the pasted block stays byte-identical to upstream's shape). Exponents are now double `0.6`/`0.73`, closer to upstream than the old `0.6f`.

  FMA contraction is disabled by `#pragma STDC FP_CONTRACT OFF` as the first line of `openfront.h`, restored with `#pragma STDC FP_CONTRACT DEFAULT` as the last line so it doesn't leak into framework code after the include in `puffercpu.c`. Deliberately not a `build.sh` flag — that would change codegen for every env. Relies on clang (gcc ignores the pragma); `build.sh` uses `${CC:-clang}` on both platforms.

  Verified: Mac arm64 (Apple clang 15, `-O0 -ffp-contract=off`) and x86_64 (clang 18, both `-O0 -ffp-contract=off` and `-O2 -mavx2 -mfma` pragma-only) give identical `hist_run` output. **Honest limits:** a forced `-ffp-contract=fast` control also matched, because `det_pow`'s result is narrowed straight to float — so the pragma is not yet shown load-bearing on the sim; it becomes so once commit 2 moves sim state to double with integer troop floors. And only the new header was run on x86, so "commit 1 fixed the old divergence" is unproven — the claim is "post-commit, Mac and x86 identical." The per-call-site check (det vs libm over a full `hist_run`) gave max rel diff 2.2e-7 / 2.6e-7, i.e. float ULPs, which rules out a transposed argument.

  Unit tests now use a local seeded xorshift (`297cad49`), so whole-file stdout compares across platforms with no masking. Verified 22 Sept on `297cad49`: `deb2937e…` on Mac and on x86 under both build configs.

  **Tier A #2a (float → double, `39db150f`) — the pragma is now load-bearing.** Mac arm64 (Apple clang 15) and x86_64 (clang 18) agree on stdout sha256 `795fe2a3…` under `-O0 -ffp-contract=off` + ASan/UBSan and under `-O2 -mavx2 -mfma`; the x86 `-O2 -mfma` build emits zero `vfmadd`, so clang honours the pragma over the flag. With the pragma stripped and `-ffp-contract=fast` on the Mac (1715 `fmadd` emitted), `hist_run` output diverges. That was not true at commit 1, where results were narrowed straight to float. Heap priorities stay float by design (every key value is exact in float).
- **Nobody else in the Puffer Discord is building this env** (confirmed). `djmango/openfront-ai` is a separate project wrapping the TS engine — not a conflict.
- **Scope: full game, staged by tier (21 Sept 2026).** Superseded the earlier v1-only plan (territorial core, everything else cut). `openfront_env_spec.md` was rewritten in full for this, anchored to upstream `7defd24` (was `fc50009`), and now specifies every state-changing system under `src/core/` except the rail routing internals (summarised, §20) and the Nation AI (partial, deferred to `openfront_nation_ai_spec.md`). Build tiers, `openfront_env_spec.md` §0: A territorial core (re-derived to the new anchor) → B gold/units/structures/cities → C naval → D nukes/SAMs → E diplomacy (paired with Phase 3 self-play) → F rail → H game modes → G Nation AI.

  **Tier A: complete 25 Sept 2026** (`4a3d2848`). The header conforms to `7defd24` for the territorial core, with its anchor comment flipped and a scope line (no boats, nukes or alliances). Per-item record in spec §25 and §1 above.

  Draft-PR sequencing: land the `build.sh` bash-3 PR (`1c37a9a3`) and the `ccache` fix (both still unopened — the fastest route to merged upstream commits during recruiting); finish Tier A; build Tier B-lite; retrain the baseline (100M, two seeds); open the draft PR. Spatial obs + conv encoder + tile-targeted action head are the first follow-up PR.

- **PR #1 stopping point: Tier A + Tier B-lite (decided 24 Sept 2026).** Recruiting is live, and the full tier plan is months of work. The current build plays like territorial.io: the agent has one decision (which neighbour to attack). OpenFront's actual decision loop is spend troops on land, spend gold on capacity (City), or spend gold to hold a border cheaply (Defense Post); community strategy guides frame the whole opening around that choice. Tier B-lite = gold income + `conquerPlayer` gold transfer, City and Defense Post only (costs, construction, placement, capture, city capacity, post attack modifier), and bot structure scrapping. Scope table in spec §0.

  **Factories deferred** (considered and rejected for PR #1). A Factory is all of Tier F — stations, A* rails with snap/split, cluster maintenance under capture, train units, saturation-driven spawn — several times the work of City + Post and the most bug-prone subsystem left. At 48×48 it's degenerate: station range 110 exceeds the map, so every City connects to every Factory and route geometry doesn't exist. With no alliances and non-building bots every stop pays the lowest tier. Cheap fallback if the compounding decision is ever needed: an abstract timer-income Factory scaled by own Cities — a real departure from upstream, only if a trained agent fails to learn City-vs-Post. Real Factories come later with naval and real-scale maps. **Nukes** are the likely second PR (the spectacle, no pathfinding).

  **Open, answer before writing B-lite code** (spec §0): economy vs episode length (first City = 1,250 ticks of human income vs a ~1,900-tick episode); defense-post radius (30) and structure min-distance (15) at 48×48; Discrete-7 → Discrete-9 (`build_city`, `build_post`) with automatic placement (City at the deepest interior tile, post on the border facing the most dangerous neighbour) — an extension of the settled action space, not a reopening; upgrades (probably out). *(The Tier A trim question is closed — see the Tier A trims bullet below.)* Each applied rescale gets a row in the rescale table.

- **Validation protocol for behaviour-changing commits (25 Sept 2026).** 20 seeds (42–61) per build via `sweep.sh`, **unpaired** — trajectories diverge at episode 0 and the RNG carries across episodes, so "seed 42 before" and "seed 42 after" are independent draws. Decide on |Δ| < 2 SE. Five seeds underestimate seed-to-seed sd (654 on five vs 1030 at twenty for tiles moved) and produced one false positive (2b). A real effect gets attributed by ablation on the pre-change header before commit; stated mechanisms are hypotheses until the ablation agrees (the 2b defender-floor story was wrong). Behaviour-*neutral* changes still need an unchanged sha, nothing less.
- **x86 check trigger rule (25 Sept 2026).** Run the Mac↔x86 hash check when a commit touches floating point, libm/DetMath calls, type widths or format strings, and always before a training run or an upstream push. Otherwise let it batch — each pass covers every commit since the last. Pure-integer commits (spawn disk, 1a, 4a, 4b) batched.
- **Tier A trims (25 Sept 2026).** Spawn disk: in (landed). Spawn phase: no code, equivalent by construction. Spawn immunity: out to Phase 3 (inert at `agent_is_bot = 1` — the agent is Bot-type, never immune, and only Human attackers respect immunity). River-crossing `nearby()`: out to Tier C — without boats, bots would pick lake-separated targets they can't reach; `bordering_players` is exactly `nearby()` minus the river and fallout terms. Fallout exclusion: Tier D.
- **Recorded divergences — iteration order (25 Sept 2026).** Upstream iterates JS `Set`s in insertion order in two places where ours walks `TileSet` order: annexation cluster formation (changes cluster indices, hence tie-breaks and each cluster's first tile) and the dead-defender pass order. Same class as the per-attack RNG (§25 item 11): reproducing it isn't worth it.
- **Known limitation — heap cap vs border set.** `atk_border_add` runs before `heap_push`; if a push were ever refused at `HEAPCAP`, the set would count a tile the heap lacks for the rest of that attack. Upstream has no cap. `heap drops` is 0 in every run (peak ~210/2048 post-1a); a nonzero count means this is live.

---

### 6. Phase plan

| Phase | Content |
|---|---|
| 0 | Throughput prototype. **Done.** |
| 1 | **Done pending audit.** Action space, punch list 1–4, spawn placement + RNG, bot handicaps, annexation, retaliation, `Env`-struct refactor, and the PufferLib 5.0 binding. Moved into the fork Sept 2026 and builds through `build.sh --cpu`. Remaining: the clause-by-clause audit. |
| 2 | **Underway.** Training works end to end (§6.1). Map gen done and audited (§1). Terrain did not move the ceiling and capacity barely did (§6.5) — the ceiling is observation. **Tier A done 25 Sept. Now: perf pass, then Tier B-lite (§5 PR #1 stopping point), then a baseline retrain.** Spatial obs + custom encoder moves to the PR after PR #1, judged by the elimination rate (~45%). Separately: a `max_steps` experiment (own control). "Beat scripted bots" is retired as a milestone — the eval metric is perf (land share at fixed horizon). |
| 3 | Self-play harness; scripted bots become held-out eval opponents via `PUF_HAS_BOT_POLICY` |
| 4 | raylib render; sweep; PR |

Draft PR opens after Tier A + Tier B-lite + retrain (§5).

---

#### 6.1 First training runs (17 Sept 2026) — the env learns

Vast RTX 3090, `./puffer train`, `num_agents=1`, `agent_is_bot=1`, `action_repeat=10`, `max_steps=200`, `total_agents=1024`, `backend=Serial`, policy 1×128 (54.1K params). Eight scripted bots as opponents.

| steps | perf (land share) | win | ep_return | ep_len | annexations | entropy |
|---|---|---|---|---|---|---|
| random baseline | 0.004 | 0.000 | −0.99 | ~59 | — | ln 7 = 1.946 |
| 65k (smoke) | 0.009 | 0.000 | −0.971 | 67.0 | 1.707 | 1.928 |
| 10M | 0.219 | 0.016 | −0.146 | 167.0 | 3.268 | 1.352 |
| 100M | 0.315 | 0.074 | +0.078 | 170.0 | 3.231 | 1.247 |
| 500M | 0.331 | 0.086 | +0.117 | 170.6 | 3.261 | 1.244 |

**Read the centers, not the samples.** `win` oscillates in a roughly ±0.04 band epoch to epoch — comparable to the movement between checkpoints — so single-dashboard deltas are noise. Two wrong calls were made during this run off single samples: "plateaued/regressed" off a 0.060 low at 227M, then "accelerating" off a 0.121 high at 415M. Neither was real; the center was ~0.09 throughout the back half. The band is most likely sampling variance — each dashboard's `win` is a windowed mean over the episodes that finished that epoch, and at ~171 steps per episode that window is small. **Compare centers across large step gaps only.**

**The curve is genuine and decelerating.** 0.016 → 0.074 → 0.086. The first 10× bought +0.058 win; the next 5× bought +0.012. Normal learning-curve shape.

**Diagnosis at 500M: capability ceiling, not a bug and not a reward problem.** Three candidate failure modes were defined in advance and distinguished by entropy:

- *Not premature convergence.* Entropy settled at 1.244, well above collapse, and stopped falling. `clipfrac` and `kl` both at 0.000 — the policy converged cleanly rather than churning.
- *Not shaping drowning the objective.* This was the live worry (§: cold-start sparsity — shape survival, keep it weak enough that the real objective dominates). It did not happen. `win` **outpaced** `perf` at every stage: win went 1.6% → 8.6% (5.4×) while land share went 0.219 → 0.331 (1.5×). And `annexations` stayed flat at ~3.26 across the entire run while wins grew — the agent is not taking more territory, it is playing the same territory better. That is the shaping handing off to the real objective, which is exactly the designed behaviour.
- *Therefore: capability.* Two candidates, both already flagged in this doc — 31 floats of flat obs describing a 2,304-tile map, or 54.1K params in a single hidden layer.

**Do not run the annexation/flat-obs diagnostic yet.** The ceiling was measured on a bare open grid; real map gen is still a pending Phase 2 item. Chokepoints and coastline give a flat obs vector structure to key on that open space does not, so the ceiling may move on terrain alone. Diagnosing now means diagnosing against geometry that is about to be replaced. **Order: map gen → re-run 100M on real terrain → compare centers. If still ~0.086, the flat-obs ceiling is real and `annex_by[p]` earns its keep.**

**Open question the run surfaced:** `ep_len` sits at ~170 against `max_steps=200`, so some share of episodes end by cap rather than by resolution. Unresolved games mechanically cap the win rate regardless of policy quality. Log the cap-vs-elimination split before reading much into any future win-rate stall.

---

#### 6.5 Terrain runs (21 Sept 2026) — the ceiling is observation

Vast 3090, 100M each, same config as §6.1 plus `map_seed = 0`, `land_frac = 0.65`. Final-dashboard values:

| policy | seed | perf | win | eliminated | timeout | rival_won | annex/ep | entropy |
|---|---|---|---|---|---|---|---|---|
| 1×128 (54K) | 73 | 0.323 | 0.244 | 0.460 | 0.287 | 0.009 | 2.18 | 1.220 |
| 1×128 | 74 | 0.336 | 0.260 | 0.449 | 0.278 | 0.013 | 2.21 | 1.227 |
| 2×512 (1.6M) | 73 | 0.354 | 0.285 | 0.435 | 0.267 | 0.012 | 2.23 | 1.271 |
| 2×512 | 74 | 0.342 | 0.277 | 0.448 | 0.262 | 0.013 | 2.20 | 1.248 |

Outcome fractions sum to 1.000 on every run. Honest throughput ~292–297K SPS for both sizes (env is 81–94% of the loop).

- **Terrain did not move the ceiling.** 1×128 mean perf 0.330 vs 0.315 at 100M on the open grid (0.331 at 500M). This was the pre-registered test (§6.1): still flat ⇒ flat-obs ceiling is real.
- **Win tripled mechanically.** The 0.8 bar fell to ~1198 tiles; land share didn't move. Games with the agent resolve 25.3% (won + rival_won) vs 24.7% bot-only — the agent replaced the bots as the finisher but games don't resolve more often. The old "agent/bot ratio 1.72, needs ~42%" framing does not survive the bar moving; don't quote it.
- **Capacity: real but small.** 2×512 mean 0.348 vs 0.330 (+0.02), both 2×512 runs above both 1×128 runs. No throughput cost, so **2×512 is the new policy baseline** (committed to the ini).
- **Elimination ~45% on all four runs regardless of capacity.** This is the number spatial obs has to move: the agent dies before the clock in nearly half of games, and 31 flat floats can't show it a threat forming along its border.
- **Seed variance: ±~0.013 perf at 100M.** Single-run perf differences under ~0.03 are noise. Replicate before concluding.
- A 10M smoke run on terrain (1×128): perf 0.244, win 0.140, eliminated 0.525, timeout 0.320.

Checkpoints (final of each run) are on the Mac at `~/summer26/ckpts/step7/run{0..4}_*.bin`: run0 10M smoke, run1 1×128 s73, run2 2×512 s73, run3 1×128 s74, run4 2×512 s74. To eval a 1×128 file after the ini change, pass `--policy.hidden_size=128 --policy.num_layers=1`.

**Mac eval/render — works.** `/opt/homebrew/bin/bash ./build.sh openfront --cpu` (Homebrew bash needed until the `build.sh` PR merges), then from the repo root `./openfront <ckpt.bin>`. Agent is seat 1, red; water blue; unclaimed land grey-green; Esc quits. `--headless --eval_episodes=N` prints metrics, and `CPU_META` shows `file_floats` vs `need`. No path = untrained policy. The binary reads `config/openfront.ini` from the cwd, so the policy shape must match the checkpoint.

#### 6.2 Operating the trainer on 5.0 — answered unknowns

**Config loading: WORKS, and the `[env]` keys arrive.** This was the top open unknown ("the 4.0 `ValueError: No config for env_name` came from the Python loader, which no longer exists, so the failure mode is unknown"). `config/openfront.ini` is read, and `num_agents` / `agent_is_bot` reach `puf_init` as written. **The proof is worth keeping as a reusable check:** at epoch 1 the dashboard showed `perf 0.009` with `ep_len 67`, which sits on the `agent_is_bot=1` random baseline (0.004, dies ~decision 55) and nowhere near the `agent_is_bot=0` baseline of 0.835. A silently-defaulted `agent_is_bot=0` would have produced a suspiciously excellent curve on a solved game. **Always sanity-check epoch 1 against the random-policy table (§6) before trusting a run.** Entropy at epoch 1 should also sit just under ln(7) = 1.946; 1.928 confirmed the policy was still uniform.

**Checkpoints.** Keys live in `[base]`: `checkpoint_dir` and `checkpoint_interval`.
- `checkpoint_interval` counts **epochs, not steps** (`src/pufferl.cu:3109`, `(epoch + 1) % checkpoint_interval`). Size it against epoch count: 10M ≈ 152 epochs, 100M ≈ 1525, 500M ≈ 7629.
- **The final epoch saves unconditionally** (line 3108), so a run always leaves at least one checkpoint even with both keys unset.
- `checkpoint_dir` unset falls back to `checkpoints/<env>/<unix_ms>/`, e.g. `checkpoints/openfront/1789679730254/0000000009961472.bin`. Filename is the zero-padded global step.
- Weights are **flat fp32 `.bin`**, not CUDA-serialized tensors. This may kill the old "Mac can't eval CUDA checkpoints" limitation, which came from `torch.load` reading `_C` serialization — and `torch` is no longer in the loop at all. **TESTED 21 Sept: works.** The Mac's `--cpu` eval binary loads Vast `.bin` checkpoints and renders them (§6.5).

**`./puffer eval` opens a render window and segfaults headless.** GLFW fails on the missing `DISPLAY`, and the failure is not checked before dereference. `xvfb-run -a ./puffer eval ...` initializes fine but prints nothing to stdout — you get an invisible 30fps window and no metrics. **Eval is a Mac activity, not a Vast activity.** Don't burn instance time on it.

**`device` defaults matter.** The ini shipped `device = cpu`; the 3090 sat at 3% for the whole first run. Set `device = cuda`. The speedup is modest because the loop is env-bound (env is 91–95% of `Evaluate`), but it stops the model from competing with the sim for cores.

**Throughput: ~290K SPS sustained.** The dashboard `SPS` field is instantaneous/last-epoch and reads high (608K seen); the honest number is steps ÷ uptime, which came out 273–290K across all runs. Use that for planning: **100M ≈ 6 min, 500M ≈ 30 min.** Env is 95% of the loop and the GPU is idle at 3–7%, so **speed is not the constraint** — if learning stalls, the fix is reward, obs, or capacity, never throughput.

For calibration against upstream: puffer.ai states most Ocean envs run 1M+ agent steps/sec per CPU core and PuffeRL trains up to 60M steps/sec. 290K is below the "most envs" line, which is expected — an Ocean toy env's step is a couple of float updates; ours is territory fill, border-set maintenance, attack resolution and a heap over 2,304 tiles with a 0.9 MB `Env`.

**Phase 3 flag: `src/pufferl.cu:1850` asserts the GPU env backend does not support selfplay or multi-policy (`match`).** OpenFront is a CPU env so this does not bite today, but Phase 3 self-play depends on that path. Know it before building on it.

---

#### 6.4 Map size, `sizeof(Env)`, and why 48x48 (20 Sept 2026)

`48x48` is a **held constant for the terrain comparison run, not a memory wall.** Earlier notes overstated the limit; the measured picture:

`sizeof(Env)` is ~938 KB and decomposes into roughly **179 bytes per tile that scales** plus **~525 KB fixed**. The fixed part is the 32 `Attack` slots, each carrying a `HEAPCAP`-sized `Heap` — independent of tile count. The scaling part is 144 B/tile of `Player` (nine players x two `TileSet`s x two `int[OF_N]` arrays each), 32 B/tile of `cl_*` / `ff_*` scratch, and 3 B/tile of `terrain` + `owner`.

`src/pufferl.cu:1000` does `calloc(total_agents, sizeof(Env))` and **line 1008 immediately `realloc`s down to `num_envs * sizeof(Env)`**, where `num_envs` is however many envs it took to sum to `total_agents`. So the struct count is `total_agents / num_agents`. With `num_agents = 1` and `total_agents = 1024` that is 1024 structs. nmmo3 runs a 512x512 map with `num_agents = 1024` and `total_agents = 4096` — **four** structs. Same framework, 256x the count.

At `total_agents = 1024`, `num_agents = 1`, host RAM:

| grid | tiles | per env | total |
|---|---|---|---|
| 48x48 | 2,304 | 0.94 MB | 0.96 GB |
| 96x96 | 9,216 | 2.2 MB | 2.2 GB |
| 128x128 | 16,384 | 3.5 MB | 3.5 GB |
| 192x192 | 36,864 | 7.1 MB | 7.3 GB |
| 256x256 | 65,536 | 12.2 MB | 12.5 GB |

So **128x128 is reachable today with no refactor**, on a Vast box with enough host RAM. Two costs: step time scales with tiles (~7x at 128x128, so ~40K SPS from 290K, and a 100M run goes 6 min → ~40 min), and episodes hit the `max_steps` cap harder, which is already the binding constraint (below).

**The `TileSet` is O(players x tiles) storing O(tiles) of information.** A tile has exactly one owner, so it is in exactly one player's `tiles` set and at most that same owner's `border` set. The `pos[]` reverse index therefore does not need to be per-player: **one global `tile_pos[OF_N]` and one global `border_pos[OF_N]` replace all eighteen**, taking 144 B/tile to ~80 with no algorithmic change — `ts_add` / `ts_remove` / `ts_has` index a shared array instead of a per-set one. The remaining per-player `tiles[OF_N]` arrays are the harder half: they sum to at most `N` entries in use but are each sized for the worst case, and collapsing them needs a shared arena with per-player segments plus compaction on transfer. Queued as a standalone refactor, not blocking anything.

This is the answer to "why 48x48" on stream: *because the territory representation is O(P·N) and I have not refactored it yet* — not "because that is what fit."

**Real OpenFront map sizes.** `map-generator/map_generator.go` sets `minRecommendedPixelSize = 2000000`, so full-scale maps are ~2M tiles. The generator emits three variants: full, `map4x` at half dimensions (1/4 the tiles, ~500K), and `map16x` at quarter dimensions (1/16, ~125K). **The smallest thing OpenFront ships is ~350x350.** At 179 B/tile that is ~22 GB at `num_agents = 1`; with the `pos[]` collapse and all eight seats promoted to agents (Phase 3 self-play anyway) it lands near 1.2 GB. Real-map-sized is a Phase 3 conversation, not an impossible one.

---

#### 6.3 Peer precedent: a contributor env in the same position

`jordanbailey00/fc-rl` — "Fight Caves RL", a deterministic C simulation of Old School RuneScape's 63-wave Fight Caves, with a PufferLib PPO integration and a Raylib viewer, open as **PufferLib PR #8**. Trains from scratch with no demonstrations and no scripted policy, three action heads (movement, target selection, Prayer). It is a *contributor* env, not a first-party Joseph env — the same position as OpenFront. Worth reading for PR structure, asset/manifest handling, and how they scoped a large sim into a reviewable diff. (No published SPS figure for it; searched, not available.)

Also confirmed from the docs during that search: PufferLib explicitly looks for `your_env.h` inside `ocean/your_env` — the current layout is right. The docs carry a "common bugs if your env is not training" checklist; read it before the PR.

**Phase 1 ordering note (kept — it held).** Action-space design came *before* the skeleton, and it was the right order: the action space decides things that change struct layout. `ACT_SIZES {7}` and `NUM_ATNS 1` fell straight out of the settled Discrete-7 space (§5) once `agent_is_bot` became a config key rather than a hardcoded choice.

**Binding: BUILT, EXERCISED, AND MOVED INTO THE FORK (Sept 2026), pending audit.** Developed in `~/summer26/openfront/` rather than directly in the fork, so an abandoned approach left no diff; moved once it stopped being abandonable. It now builds through upstream's own `build.sh openfront --cpu` and the resulting `./openfront` renders — which was the last unexercised path, since `drive_test.c` stands in for `pufferl.cu` and not for `puffercpu.c`.

Built: preamble (`obs_t`, `ACT_SIZES`, `NUM_ATNS`, `OBS_SIZE 31`, `_Static_assert` tying `OBS_SIZE` to `6 + 5*ACT_NEIGHBORS`), `Log` / `Seat` / required `Env` fields, `sorted_neighbors`, `compute_observations`, `apply_action`, `add_log`, `puf_reset`, `puf_step`, `puf_render`, `puf_init`, `puf_log`, `puf_close`, and `openfront.ini`.

**`drive_test.c` is the load-bearing new artifact.** The `openfront.c` harness never calls a single `puf_*` function, so the entire binding was unexecuted code — it compiled and proved nothing. `drive_test.c` stands in for `pufferl.cu`: it builds a `Dict`, sets `rng = <env index>` before `puf_init` exactly as the framework does, wires four envs into one contiguous obs/action/reward/terminal block, drives uniformly random actions, and asserts no NaNs, observations inside [0,1], and that episodes terminate. Run it after any change to the binding; nothing else executes that path.

It caught two defects on first run:

| | what | root cause |
|---|---|---|
| 1 | Observations reached 1.8, spec'd [0,1] | `nb_shared` counts **adjacencies, not tiles** — one border tile with all four neighbours owned by `q` contributes 4, so `nb_shared/border` maxes at 4, not 1. Now normalised by total contested adjacency (a share of the frontier, which is what the attack decision turns on); absolute frontier scale is already `obs[2]`. |
| 2 | `Log.annexations` summed **all eight players'** annexations | The counter exists to test whether the flat obs ceiling is real, so attributing it to everyone made it useless. Added `annex_by[MAXP]`, stamped in `annex_remove` with the capturer, and the Log reads `annex_by[p]`. |

Both are the same class as the §4 audit defects: things that compile clean, pass every existing invariant, and are only visible to a harness that can express them. **Extend `drive_test.c` first whenever a new binding invariant is discovered.**

Measured through the binding, uniformly random actions, 4 envs:

| config | mean land share | wins | seat-0 lifespan | mean return |
|---|---|---|---|---|
| `num_agents=1`, `agent_is_bot=0` | 0.835 | 32/32 | 39.8 | +1.81 |
| `num_agents=1`, `agent_is_bot=1` | 0.004 (dies) | 0/30 | 59.0 | −0.99 |
| `num_agents=8`, `agent_is_bot=1` | 0.150 (≈ even split) | 0/20 | 150.9 | −0.32 |

*(Third recorded version of this table; the first two were wrong for different reasons and both are worth remembering. **v1** — 0.804 / 0.026 / 0.071 — was measured with `sim_tick` running the scripted driver on policy seats as well, so every agent seat was co-driven by a bot. **v2** — 0.766 / 0.000 / 0.119 — fixed that but predated the `annex_capturer` tile-counting fix, which changes capturer selection and therefore every downstream trajectory. Both defects came out of the Claude Code audit. The qualitative conclusions survived both corrections unchanged, which is the only reason the `agent_is_bot` decision didn't have to be revisited.)*

Corresponding bot-only harness baseline after the same fix: **300 episodes, 15 wins (5.0%), mean 1988 ticks, 53.6% eliminated, 5831 annexations, 71444 tiles moved, heap peak 350.** Down from 27 wins / 5743 / 77929 — capturer selection moved, so who inherits which cluster moved, so every trajectory moved. Elimination rate held at ~53.7% across the change, which is the sanity check: the *rate* of territory changing hands is stable, only *who receives it* changed.

**Resolved 21 Sept by Tier A commit 1 — see §5 "Cross-platform determinism". The rest of this block is history.**

**(History) Cross-platform determinism holds for the binding and NOT for the long harness run — open question, do not claim bit-identity unqualified.**

- `drive` matches bit-for-bit between an M3 Pro and an x86 container across all three configs, to four decimals, over 120 episodes each.
- `of_dbg`'s 300×2000-tick `hist_run` does **not**: M3 gives 15 wins / 5831 annexations / 71444 tiles / heap peak 350, container gives 13 / 5795 / 70052 / 355. Before the `annex_capturer` fix the same run *was* identical across both (27 wins on each), so this appeared with that change.

Ruled out so far: `run_tests` contamination (`of_dbg` and `of_fast` agree exactly on the same machine, and `run_tests` builds its own Envs), and `rand()` (every call site is inside a test function). Leading hypothesis is a 1-ULP `powf` difference between glibc and Apple's libm — `powf(tiles, 0.6f)` and `powf(troops, 0.73f)` run every tick for every player — amplified by a chaotic sim over ~600k ticks, with the fix having moved the trajectory onto a knife-edge float comparison that the old one missed.

**Bisect procedure when this gets picked up:** print `env_hash(e)` after each episode in `hist_run` and find the first episode that differs. Episode 0 differing points at an early float comparison; divergence at episode 50 points at accumulation. Then narrow within that episode by tick. Practical impact today is nil — no decision depends on the harness number and the binding path is clean — but "deterministic across platforms" is a claim that will get made on stream, so it needs to be either true or qualified.

**Not built, deliberately:** `openfront.cu` (no custom encoder — v1 has no spatial obs), `openfront_net.h`, action masking (`action_mask = NULL`; invalid neighbour slots fall through as noop).

**PufferLib structural requirements — 5.0. `binding.c` DOES NOT EXIST on this branch.** The three-file `openfront.h` / `openfront.c` / `binding.c` plan recorded here previously was the 4.0 layout, carried over from `vast_pufferlib_setup_reference.md`, which documents a 4.0 fork. Read off a shallow clone of 5.0 at `ba238f8` (21 Aug 2026):

```
ocean/openfront/openfront.h      struct Env + puf_init / puf_reset / puf_step
                                 / puf_log / puf_render / puf_close
ocean/openfront/openfront.cu     optional custom CUDA encoder
ocean/openfront/openfront_net.h  optional CPU policy for the eval main
config/openfront.ini             `openfront` in [base] env_name
```

**`<env>.c` IS OPTIONAL AND WE DON'T HAVE ONE (Sept 2026).** `build.sh` line 226 reads `SRC_FILE=${SRC_FILE:-$SRC_DIR/$ENV.c}`, but header-only envs never reach it — `src/puffercpu.c` includes the env header directly (line 612) and supplies the main itself. Upstream commit `7224706b` deleted `ocean/admiral/admiral.c` outright and kept only the header, which is the precedent. **The shipped env is two files: `ocean/openfront/openfront.h` and `config/openfront.ini`.**

This is why `openfront.c` had to be renamed before the move (it was the statistics harness, with its own `main()` — two mains, framework's wins). It is now `harness.c` in `openfront-proto`, and the name it vacated turns out not to be needed at all.

- The env is a **single header**. `typedef float obs_t;` must appear *before* `#include "pufferenv.h"`, then `#define OBS_SIZE`, `ACT_SIZES`, `NUM_ATNS`. `build.sh` hard-errors if the header does not typedef `obs_t`.
- **No `my_init`, no kwargs struct.** `puf_init(Env*, Dict*)` reads config with `dict_get(kwargs, "key")`; `puf_log(Log*, Dict*)` writes with `dict_set`.
- `struct Env` must carry the framework's required fields (`Log log`, `Agent agents[]`, `int tag`, `int boundary_reached`, `int num_agents`, `unsigned int rng`). `struct Log` must have `float perf` first and `float n` last.
- **`sizeof(Env)` is a hard budget.** `src/pufferl.cu` does `calloc(total_agents, sizeof(Env))` — one `Env` per agent slot, allocated before it knows how many envs it needs. Large per-env arrays go behind pointers allocated in `puf_init` and freed in `puf_close`; see `ocean/moba/moba.h` for the pattern. Our 0.89 MB `Env` is why `HEAPCAP` was resized (§3).
- `envs[i].rng = <env index>` is assigned **before** `puf_init` runs. Seed scrambling is mandatory, not hygiene (§3).
- ~~**Config loading is unverified on 5.0.**~~ **Answered (Sept 2026): it works.** See §6.2. `config/openfront.ini` is read and `[env]` keys reach `puf_init`. A clean `--cpu` build still does not prove the ini is well-formed — the epoch-1 sanity check against the random-policy table does.
- Fastest read on what changed: read `ocean/minimal/` for the smallest complete example, and `ocean/admiral/` for a header-only env.

**Branch target: 5.0, not 4.0.** Joseph in #development, 16 Aug 2026: "Current branch is 5.0." The 5c branch was deleted and its open PRs merged manually; a stable 5.0 prerelease with SOTA on all major envs was being stabilized that weekend. Sam's fork is on 4.0 and needs `git checkout -b 5.0 upstream/5.0`.

**Branch move: DONE (Aug 2026).** `samuelpshi/PufferLib` is on `5.0`, tracking `origin/5.0`. Upstream force-pushed `5.0` (`c3e28db1...ba238f8c`) and cut a `5.0-backup` at the same time, so history gets rewritten under the fork — don't pin work to a 5.0 commit hash, and re-merge before the PR.

**Merged to `6ffa5b10` (Sept 2026), 157 commits, contract verified intact.** Procedure worth repeating on the next merge, because it is cheap and it is what makes the merge a non-event:

1. Diff only the surface the binding depends on *before* merging — `git diff HEAD..upstream/5.0 -- src/pufferenv.h src/puffercpu.c build.sh`. Everything else is new envs and sweep configs.
2. After merging, rebuild and rerun `drive`. The bar is bit-identical: `perf=0.8345`, `win=32`, `ep_len=39.8`.

This merge's diff to `pufferenv.h` was +7/−409: the 409 were the WIP mascot vertex-cache loader moving out, and the 7 were the bot-ladder hook below. All six `puf_*` signatures, `Agent`, and `Log` unchanged. `drive` came back bit-identical.

**`PUF_HAS_BOT_POLICY` / `puf_set_bot_policy(Env*, int)` — new, and it is Phase 3's mechanism.** Upstream's comment: the bot ladder writes it between rungs so one PuffeRL can eval a whole ladder; envs with scripted opponents `#define PUF_HAS_BOT_POLICY` and assign `env->bot_policy`. That is exactly the "scripted bots become held-out eval opponents" plan, now first-class rather than something to hand-roll. Don't build to it before Phase 3, but it changes that phase from "write a harness" to "implement one hook," and it is worth mentioning on stream that the env fits a feature they already shipped.

**Python is gone. `92e321a5 Purge python`.** There is no `pip install`, no `puffer` CLI, no `pyproject.toml`. `pufferlib/` holds stale 4.0 artifacts (`_C...so`, `__pycache__`) and `pufferlib.egg-info` says Version 4.0.0 — leftovers, not a package. **The env is compiled into the trainer binary**: `./build.sh openfront` produces `./puffer`, run as `./puffer train|eval|match|sweep [--section.key=value ...]` (equals sign, underscore in the key: `--train.total_timesteps=100000`). Switching envs means rebuilding, not changing an argument.

**Mac cannot train, confirmed (Sept 2026).** `./build.sh openfront` (no flags) is `MODE=native`, which compiles through `nvcc` and dies on any machine without CUDA. `--cpu` is a different target: the standalone eval main, which builds and renders fine on the M3. So the local loop is `mk.sh` plus `./build.sh openfront --cpu`; all training is Vast.

**`build.sh` is broken on macOS and the fix is a one-line upstream PR.** Line 202 uses `-DPUFFER_${ENV^^}`, a bash 4.0+ expansion; macOS ships bash 3.2.57 as `/bin/bash` and will not update it (GPLv2), so `./build.sh <env> --cpu` fails immediately with `bad substitution` for every macOS contributor. This is the documented "mostly for Mac users" path. Portable fix, verified building and running `minimal` on an M3 Pro:

```bash
EXTRA_CFLAGS+=(-DPUFFER_$(echo "$ENV" | tr "[:lower:]" "[:upper:]"))
```

`tr` is POSIX, so it doesn't trade a Mac break for a Linux one. It is the only bash-4 construct in the file. **File this separately from the env work** — one line, obviously correct, no design opinion, and merged upstream PRs are the only thing that reaches the contribution graph (§8).

**Still unfixed upstream as of `6ffa5b10` (Sept 2026)** — the expansion is now at line 208.

**RESOLVED (20 Sept 2026): committed and pushed.** Branch `fix-build-sh-macos-bash3`, commit `1c37a9a3` ("build.sh: replace bash 4 case expansion with POSIX tr"), tracking `origin/fix-build-sh-macos-bash3`. The earlier warning about an uncommitted ` M build.sh` being the sole copy is obsolete — ignore it if it resurfaces elsewhere.

Still to do: **open the PR.** Until it merges, the Mac `--cpu` build needs Homebrew bash: `brew install bash`, then `/opt/homebrew/bin/bash ./build.sh openfront --cpu`. The commit exists on the fork; nothing has been submitted upstream. It should go out ahead of the env work. It is not an OpenFront feature — it's an independent one-liner against a broken upstream path, and merged upstream PRs are the only thing that reaches the contribution graph (§8).

**Second candidate, same class: `ccache` is invoked unconditionally.** The `native` build dies at `line 506: ccache: command not found` on any machine without it, which is most fresh machines. One-line fix (`command -v ccache >/dev/null && CCACHE=ccache || CCACHE=""` and use `$CCACHE`). Separate PR — separate concern, and bundling two unrelated fixes weakens both.

**No libomp fight on 5.0.** The `build.sh` / `src/vecenv.h` OpenMP workaround from the 4.0 fork (saved at `~/summer26/mac-libomp-workaround-4.0.patch`) was not needed; `minimal` builds clean after the bash fix alone. Keep the patch, don't apply it preemptively.

Known 5.0 changes still to verify:
- **Custom encoders moved into the env's own folder** (Joseph, 3 Aug: previously `ocean.cu`, moved out to avoid bloating src line count with single-env encoders). Not needed for v1 — flat obs, no encoder — but it lands the moment spatial obs does.
- ~~**CUDA envs are now common** — confirm a plain C env is still first-class.~~ **Answered (Sept 2026): yes.** A header-only CPU env builds and runs through upstream's own `build.sh --cpu` with no `.cu`, no `_net.h`, and no `.c`.
- ~~Joseph mentioned a forthcoming **"refactor skill"**; at least one contributor is holding a PR until it's published.~~ **Published (Sept 2026): `SKILL_ISSUES.md` at the repo root.** Hand-written C/CUDA style and refactoring doctrine: inline single-use functions, replace defensive checks with plain asserts, preallocate at init and let the OS free, no forward declarations, headers are source not declaration lists, soft 80 / hard 100 col, 4-space indents, no bare scoping blocks, do not split code into more files. New code is written to it; the whole-file conformance pass is scheduled immediately pre-PR, when the file has stopped moving.

**Timing note:** 5.0 is mid-stabilization, not mid-expansion. A new env PR landing during that window gets less attention. The Phase 2 timeline puts the draft PR well past it anyway, but it argues against rushing a skeleton onto the branch early.

---

### 7. Working conventions

**The split — CHANGED Aug 2026, standing decision.** Claude writes the game simulation code; Sam audits it. Puffer binding setup is done together. This replaces the earlier convention (Sam writes all core sim, Claude writes only plumbing), which had been overridden per-message for a while without the file being updated.

**The audit is the load-bearing half, not a formality.** The reason the old convention existed has not gone away: Joseph reviews PRs on stream and asks implementation questions. Anything Sam cannot explain unprompted is not done. Two things in the current file are worth being able to explain cold, because neither is derivable from the spec alone:

- Why the annexation cluster fill runs over the **border set** and not the territory. (A territory with a hole is one connected territory but two border components — outer perimeter and the rim of the hole. That distinction is exactly what the rule needs, and it is why cost is O(border).)
- Why the enemy bounding box must **contain** the cluster box rather than the reverse. (§9 of the spec; the reverse reading can never fire.)

The old §7 text below still describes what went wrong the first time the split broke, and the recovery rule still applies whenever Sam says "just give me the code" or "I'm confused": do not paste a body, trace one tile or one number through the operation concretely.

**What went wrong once, so it doesn't recur.** Through `attack_tick` the split worked — structure and constraints from Claude, code from Sam, four real bugs surfaced and fixed, function understood. Then Sam signaled friction ("j give me the code," "idc u choose") and Claude responded by handing over *more finished code faster*: `atk_push` verbatim, the combat math block verbatim, `player_tick` verbatim. Those were signals to stop and find where the mental model broke, not to accelerate past it. Sam ended up saying he had no idea what the code did.

**The recovery, which is the actual rule:** when Sam says "just give me the code" or "I'm confused," do **not** paste a body. Trace one tile or one number through the operation concretely — specific tiles at specific coordinates, specific arithmetic. Diagnostic questions like "why does a tile with two attacker-owned neighbors get conquered first?" and "why did 500 troops buy 6 tiles when the placeholder bought 64?" This unblocked swap-remove, neighbor-packing, the priority formula, and the combat arithmetic. The bot driver afterward was written entirely by Sam.

**Register:** state the design decision and the reason. One question back per message, max, and only at a real fork. Don't run phases ahead of where the code is. Verify handed-over code compiles and passes in the container before handing it over.

**Recurring C bug classes.** Python habits: missing semicolons, missing return types, missing braces on multi-statement `if`, `len()`, undeclared loop variables. C-specific: `=` vs `==`, `.` vs `->`, struct-by-value where a pointer was needed, nested function definitions (clang rejects), integer division where float was needed (`/100` vs `/100.0f`), `continue` inside a nested `for` continuing the wrong loop (needs a flag + `break`). And the two that the compiler will never catch:

- **Tile-number vs player-id confusion.** Everything is a bare `int`. Tiles are `t`, `nb[k]`, heap contents. Players are `p`, `attacker`, `target`, `owner[...]`. Comparisons across the two compile fine and mean nothing.
- **`heap_push` argument transposition.** `int` and `float` convert silently in both directions.

**The same bug class in TypeScript, which is how two spec defects got in.** Any call whose arguments share a type is a transposition trap regardless of language, and reading the call site instead of the declaration is how you fall into it. `inscribed(outer, inner)` invoked as `inscribed(enemyBox, clusterBox)` reads as "enemy inscribed in cluster" and means the opposite; `nextInt(min, max)` was assumed inclusive because the call sites looked like inclusive ranges. **Read the signature, not the call site.** And if a rule you have transcribed turns out to be unfireable, that is evidence the transcription is wrong, not that the rule is vestigial.

**Hand-merge failure mode, seen once (Aug 2026).** Claude handed over the spawn/RNG changes as *excerpts* rather than whole functions; merging them by hand produced three defects, all of which compiled clean:

1. `bots_init` lost `bots[i].neighbors_tn = 1;` — the excerpt stopped one line short of it. `bots` is a global so it zero-inits, `bot_tick` then always took the else branch, which needs bordering players, and nobody borders anybody at spawn. **Completely dead sim**: 8 players frozen at their starting tiles for 300 ticks.
2. `attack_start` kept the *old* `players[attacker].troops -= troops;` after `heap_init` as well as the new one at the top → every attack cost double. Nastier than (1) because nothing crashes; attacks just die early and expansion looks weak, which reads as a balance problem.
3. `fill_terrain` left on `rand()` — not fatal, but defeats the point of seeding.

Lesson: when a change touches scattered lines inside an existing function, hand over the **whole function**, not the changed lines. Excerpt boundaries are exactly where adjacent required lines get dropped.

---

### 8. Resume / PR claim discipline

Only claim what is built and shipped. In-progress work gets "prototyping" framing. No feature lists — only mechanics that change the RL problem are worth mentioning.

**True and defensible now (pre-Tier-A build — refresh after the retrain):**
- ~~~1.02M ticks/sec~~ — **stale.** That was `phase1-baseline`, pre-Tier-A. The Tier A build benches ~525k (M3 Pro, `-O2`). Do not quote a throughput figure until the perf pass and a re-measure on the shipping header; bullet 2 of the resume draft below depends on it.
- ~35% average land share at 100M steps (**pre-Tier-A build** — combat, frontier and annexation have all changed since; refresh after the retrain) (2×512: 0.354 / 0.342 on seeds 73/74, §6.5) in 8-player games; random play 0.4%. **"Nearly 3× an even split"**: by symmetry, the bots' average share can't exceed 1/8 = 12.5%, and the agent plays with the same bot handicaps (`agent_is_bot=1`), so the comparison is fair.
- Output byte-identical across arm64 and x86_64 compilers, with hashed baselines gating each behavioural change.
- O(1) incremental border maintenance as an original design decision, not an optimisation over a prior version.

**Do not write any speedup ratio against the existing implementation** ("400×", "50×", or anything else). The comparison isn't controlled — different map size, mechanic coverage, no obs encoding on our side, unknown hardware on his (see §1). The honest version: the existing approach wraps the TypeScript engine at ~2,100 ticks/sec, which is why a native C sim was worth building. That's a design rationale, not a speedup claim. Someone on stream will ask.

**Do not write "validated against the original."** No differential test against the TS engine exists. What exists is invariant tests, a spec derived from source, and cross-platform determinism.

**Do not report win rate.** It's cap-limited (§1): most games hit the 2,000-tick cap unresolved.

True now: Tier A conformance to upstream `7defd24` for the territorial core, with a golden-vector combat test and a scenario test (`annex_hole_test`) that fails on the pre-fix code. Say "reimplemented from the source's mechanics", never "validated against the original".

Not yet true: maintainer review, open or merged PR, "contributed", anything about structures or gold. GitHub fork commits don't appear on the contribution graph — only merged upstream PRs do. Verify the commit author email matches a verified GitHub account email before opening the upstream PR.

**Resume bullets (drafted 24 Sept, one-line length; adoption unconfirmed):**

```
OpenFront RL Environment – PufferLib | C	May 2026 – Present
Built an 8-player territory-conquest environment in C for training reinforcement learning agents
Reimplemented the game's core mechanics from the original source, running at ~1M simulation steps per second
Designed observation spaces and reward structures; trained agents to ~35% average land share in 8-player games, nearly 3× an even split
Wrote a test suite verifying the simulation's mechanics and producing identical results across platforms
```

After Tier B-lite: add "and structure economy" to bullet 1. If the retrained agent demonstrably builds Cities/Posts under pressure rather than only expanding, that result replaces the land-share line.

---

## Archived: spec sections removed in the 25 Sept 2026 restructure

Verbatim. §0 item 6 and §1 were closed; §25 was compressed to its status table and carry-forward notes; §27–§28 were stale copies of the `fc50009`-era code state and conventions, replaced by the header itself and the project reference.

### Spec §0, open decision 6 (closed)

6. **Tier A trim — decided 25 Sept 2026.** Spawn disk landed; spawn phase is equivalent by construction; spawn immunity moves to Phase 3; river-crossing `nearby()` moves to Tier C (it needs boats). The combat rewrite (item 1) may not: defense posts plug directly into the §9 formulas.

### Spec §1, corrections to the `fc50009` edition (all applied)

#### 1. Corrections to the `fc50009` edition — read first

These override anything in older handoffs, chat history, or the previous edition.

**Upstream drift since `fc50009`:**

1. **Combat model rewritten (§9).** The per-tick "tiles budget" (`attackTilesPerTick`) is gone. Each attack now spends a tick budget of exactly 1; every conquered tile consumes a `tickFraction`. The vs-player attacker-loss and speed formulas are new; the sigmoid defender debuff and the `>100k tiles` large-attacker branches are replaced by a log-logistic territory bonus centred at 300k tiles. Terra-nullius losses and the Human/Nation-vs-Bot ×0.7 survive unchanged.
2. **Annexation largest-cluster selection changed (§11).** Single-cluster fast path; and if the largest border cluster is a *hole* in the player's own territory, the largest non-hole cluster takes the largest-cluster rule instead.
3. **`isOnEdgeOfMap` now includes 4-adjacency to impassable terrain (§4).** Consumed by both annexation surround tests and `isEnclosed`.
4. **Attack troop deduction uses the floored amount actually removed (§7).** Previously a fractional request left the attack holding unpaid troops.
5. **Win percentage is a function of elapsed time** (overtime mode, off by default) and the win check has a hard 170-minute limit (§12).
6. **Spawn phase lengths changed**: 100 singleplayer / 150 random spawn / 200 multiplayer (was 100/150/300) (§13).
7. **Map generator**: small lakes/islands being removed are now replaced by the majority neighbouring terrain type (can be impassable), not forced to land/water. Irrelevant to procedural C maps; relevant to any offline map bake.

**Present at `fc50009` but missing from the previous edition:**

8. **Player troops are integers.** `PlayerImpl._troops` is a `bigint`; every add/remove goes through `toInt` = `floor` (§2). Attack troops are plain doubles. The C sim used `float` for both; since Tier A #2a it uses `double` for both, and #2b moves player troops to `int64_t` with floor helpers (§25).
9. **Every `AttackExecution` owns a `PseudoRandom(123)`** — a fixed seed, identical for every attack. Priority jitter and the `borderSize` jitter replay the same sequence per attack. Stream fidelity is not a goal; record it as a known divergence (§2.3).
10. **Manual retreat is delayed, not instant (§7.4).** Ordering a retreat freezes the attack for 20 ticks, then returns survivors with a 25% malus vs players.
11. **Spawn immunity (§13.4)**: humans and nations cannot be attacked *by humans* during the spawn phase plus 50 ticks. Bots are never immune; non-human attackers ignore immunity.
12. **The spawn disk is 52 tiles, not 49 (§13.2)**: the Euclidean-4 test is taken from a centre shifted by −0.5 in x and y.
13. **"Neighbours" crosses rivers (§5.4)**: `nearby()` adds players and terra nullius up to 4 water tiles away, sampled from every 10th shore border tile, and ignores unowned fallout.
14. **The bot driver sends boats (§14)**: a target that does not share a land border is attacked by transport ship; terra nullius across water is expanded into by boat.
15. **Dead-defender wipe calls `conquerPlayer` on every trigger (§10)** (gold transfer), and its "someone else captures" branch tests friendliness against the *target*, not the attacker.
16. **Defense posts do not shoot at this anchor (§17.2).** `DefensePostExecution.shoot()` exists but is never called; posts act only through the attack modifier.

---


### Spec §25, conformance delta (full, pre-compression)

#### 25. Conformance delta: current C sim vs this spec

What the shipped `openfront.h` (originally built to the `fc50009` edition) had to change to conform to Tier A. **Tier A is complete as of 25 Sept 2026 (`4a3d2848`).** Each item is behaviour-changing and gets its own commit and a new `hist_run` baseline. **PR #1 column** per §0: *in* = required, *trim?* = may move to a later PR, *inert* = no effect until a later tier, *later* = out of PR #1.

**Status (25 Sept 2026): complete.** Every *in* item done or verified conformant; *trim?* items resolved (7 partly in, 8 moved to Tier C); four gaps found that this list did not name (clamp, frontier, two capturer rules). Commit hashes after the 5.0 `filter-branch`: clamp `eea12848`, 2a `39db150f`.

| # | Item | PR #1 | Status |
|---|---|---|---|
| 1 | Combat math | in | **done** — 1a border set `908b785a`, 1b formulas `c4faca4a` |
| 2 | Integer player troops | in | **done** — 2a `39db150f`, 2b `84ebc825` |
| 3 | DetMath + FP contraction | in | **done**, verified Mac↔x86 |
| 4 | Annexation | in | **done** — 4a capturer `9b542489`, 4b hole selection `d6aef90c`; fast path is a no-op for us |
| 5 | Attack init | in | **done** (floored deduction via 2b); land-only combination inert until boats |
| 6 | Manual retreat | later | — |
| 7 | Spawn | trim? → partly in | **disk done** `5175a70e`; phase equivalent by construction; immunity → Phase 3 |
| 8 | Bot driver | trim? / inert | river `nearby()` → Tier C; fallout → Tier D; scrapping live with B-lite |
| 9 | Dead-defender wipe | in | **conformant**, no code; `conquerPlayer` gold live with B-lite |
| 10 | Win check | inert | — |
| 11 | Per-attack RNG | keep ours | recorded divergence |
| 12 | `relinquish` | later | needed by nukes |

1. **Combat math** (§9) — **done.** **1a (found during 1, not in the original list):** `borderSize` is the attack's deduplicated border set (§7.2), not heap size; the C sim used `heap.count`, 1.91× too large. `borderSize == 0` takes exactly one valid tile. **1b:** replace the per-tick tile budget with `tickBudget = 1` and per-tile `tickFraction`; new vs-player loss and speed formulas; log-logistic territory bonuses (≈1 at 48×48). TN losses and ×0.7 unchanged. Must land before defense posts, which modify these formulas.
2. **Integer player troops** (§2.1) — **done.** Split in two. **2a (done, `39db150f`):** all sim math float → double, with no floors — precision only; verified by lockstep trace against the float build (max relative troop diff 3.8e-6 players, 4.2e-5 attacks normalised to start troops, no tile or attack-set divergence through tick 1200 of episode 0) and 5 seeds (win delta inside one seed sd). Heap priorities stay float (every key is exact in float). **2b (done, `84ebc825`):** player troops → `int64_t`, every write through `troops_set` / `troops_add` / `troops_remove` with upstream's semantics (`removeTroops(x ≤ 0)` returns 0; decay truncates toward zero because negation precedes the floor). Expected behaviour change: defender loss `< 1` floors to zero. Measured: no detectable aggregate effect over 20 seeds (|t| ≤ 0.8).
3. **DetMath port + no FP contraction** (§2.2). **Done.** Closed the old cross-platform `hist_run` divergence as far as can be shown ("post-commit, Mac and x86 identical"; the pre-commit divergence was never bisected).
4. **Annexation** (§11): single-cluster fast path — **a no-op for us** (with one cluster the general path already runs `surroundedBySamePlayer`; the extra `isFriendly` check is inert); hole-aware largest-cluster selection — **done (4b)**; `on_map_edge` counts impassable-adjacent tiles (no-op until impassable exists, then required in the same commit as impassable). **Found during 4 (4a, done):** `getCapturingPlayer` counts every adjacency via `getMode` (the C sim counted once per tile per owner) and breaks ties by first encounter — both for `getMode` and for the largest-attack scan (the C sim used lowest id / slot order). **B-lite note:** `removeCluster` calls `conquerPlayer(capturer, p)` when the collected set is p's whole territory — a second gold-transfer call site alongside item 9.
5. **Attack init** (§7.1): floored actual deduction — falls out of 2b's `troops_remove`; combination only when the *new* attack is a land attack (inert until boats).
6. **Manual retreat** (§7.4): 20-tick freeze, then 25% malus vs players. Relevant when retreat becomes an agent action.
7. **Spawn** (§13): 52-tile centre-shifted disk — **done** (integer form `(2dx+1)² + (2dy+1)² ≤ 64`; moves the wipe/annex threshold 16 → 17 via `SPAWN_TILES/3`); spawn-phase length — **equivalent by construction** (nothing executes during it and all spawns are placed at reset); spawn immunity (only binds if the agent seat is Human-type and attacked by Humans — i.e. Phase 3 self-play) — **deferred to Phase 3**.
8. **Bot driver** (§14): traitor step (inert without alliances); `nearby()` with river crossing and fallout exclusion; boat attacks for non-land-bordering targets and TN across water (requires Tier C); alliance acceptance (inert until Tier E); structure scrapping (**live once Tier B-lite lands**).
9. **Dead-defender wipe** (§10): friendliness tested against the target (inert without alliances/teams); `conquerPlayer` per trigger (gold transfer — **live once Tier B-lite lands**). Trigger, attacker-first rule, N/S/W/E neighbour order and ≤100 passes were checked against `AttackExecution.handleDeadDefender` and already conform. **Recorded divergence:** upstream iterates the target's live tile `Set` in insertion order; ours walks `TileSet` order, which changes which tiles become attacker-adjacent within a pass.
10. **Win check** (§12): fallout-excluded denominator (inert until Tier D); the 170-minute limit is far beyond the env's `max_steps`.
11. **Per-attack RNG** (§2.3): upstream seeds every attack with 123; the C sim draws from `e->rng`. Keep ours; record the divergence.
12. **`relinquish`** (§5.2): needed by nukes; the C sim has no owner→unowned path yet.

**Fix outside this list — attack-troops clamp (`eea12848`).** §7.3 already said attack troops clamp at 0 on every write; the C sim did not clamp, so an attack could end a tick negative and be read by `attack_start`'s cancel/combine before its next tick. Found by a new DEBUG invariant during 2a; the clamp fires ~11k times per 300-episode `hist_run` yet the harness output was unchanged.

Rescales already applied in C (threshold 17 since the spawn-disk commit — was 16 — radius 4, min-dist 13, sigmoid debuffs dropped) remain valid decisions for the 48×48 training config; item 1 removes the sigmoid debuffs from source anyway.

---


### Spec §27–§28, carried from the `fc50009` edition (stale at archive time: pre-Tier-A types and signatures)

_§27 and §28 are carried verbatim from the `fc50009` edition (its §15 and §16). They describe the C code as it stands, which conforms to that edition, not this one; §25 lists the gap._

#### 27. Code state & API contract (openfront.h)

**The sim lives in `openfront.h`, canonically at `~/summer26/PufferLib/ocean/openfront/openfront.h` and nowhere else (Sept 2026).** It began as a standalone `proto.c` answering one question — ticks/sec of the territory loop, gate ≥500k, passed — and is now the complete v1 mechanics implementation plus the PufferLib 5.0 binding, shipped as a single header. The dev tools live in `~/summer26/openfront` (`samuelpshi/openfront-proto`) and reach it by include path: `harness.c` (statistics + bench main, formerly `openfront.c`), `drive_test.c` (binding driver), `mk.sh` (builds all three binaries). One copy of the header, always — a duplicate drifts, and then the audited file is not the shipped file.

```bash
./mk.sh                          # of_dbg (tests + check_borders + hist_run), drive, of_fast
./cxxcheck.sh                    # C++ syntax gate — the header is compiled as C++ by nvcc
./build.sh openfront --cpu       # -> ./openfront, framework eval main + raylib render
```
Run `mk.sh` **and** `cxxcheck.sh` before pushing; `mk.sh` compiles the header as C, and the training path compiles it as C++. Two defects of that class shipped undetected in one day. Static asserts are spelled through `OF_STATIC_ASSERT` for this reason (`static_assert` under `__cplusplus`, `_Static_assert` otherwise).
`-lm` is required (`powf`); missing it is a link error, not a compile error. `-DDEBUG` is load-bearing — without it `run_tests()` and `check_borders()` compile to nothing and the program passes silently.

**MACRO RENAME — `W`/`H`/`N` ARE NOW `OF_W`/`OF_H`/`OF_N` (Sept 2026).** Mandatory, not cosmetic. The unscoped names collided with upstream: `src/algo.cu` and `src/pufferl.cu` use `N`, `H` and `B` as ordinary local variables and struct members, so the preprocessor rewrote upstream's own declarations into garbage (`int (48*48)`, `int 48`) and produced 100+ nvcc errors. The `--cpu` path never hit it because it includes different upstream sources. **Any unscoped macro in a PufferLib env header is a latent break for every contributor** — prefix everything. Rename verified mechanical: 49 lines, 50/50 insert/delete, full test suite and 300-episode statistics bit-identical afterward. Elsewhere in this spec, `N` in prose/formulas means the tile count `OF_N`.

**ALL SIMULATION STATE LIVES IN `struct Env` (Aug 2026).** There are no simulation globals. Every function below takes `Env *e` as its first argument. This is the precondition for PufferLib, which steps envs under `#pragma omp parallel for` — a shared array or generation counter corrupts nondeterministically and only under parallel vec.

**Signatures — exact, so cross-chat snippets don't transpose args:**
```c
#define OF_W 48
#define OF_H 48
#define OF_N (OF_W*OF_H)
#define MAXP 9            /* players 1..8; 0 = unowned */
#define HEAPCAP 2048      /* was 8*N; see note below */
#define MAXATK 32
#define START_TROOPS_HUMAN 25000.0f
#define START_TROOPS_BOT   10000.0f

/* pure — compile-time map shape only, no env */
int  ref(int x, int y); int rx(int r); int ry(int r);
int  neighbors(int r, int *out);            /* N,S,W,E order; returns count */
int  neighbors8(int r, int *out);           /* 8-connectivity, annexation only; up to 8 */
int  on_map_edge(int t);
float within(float v, float lo, float hi);

/* container-only — no env */
void ts_init(TileSet*); void ts_add(TileSet*, int t);
void ts_remove(TileSet*, int t); int ts_has(TileSet*, int t);
void heap_init(Heap*);
int  heap_pop(Heap *h);                     /* returns tile, -1 if empty */

/* everything else takes Env* FIRST */
void rng_seed(Env *e, unsigned int s);      /* splitmix32 scramble, then coerce 0 */
int  rng_below(Env *e, int n);              /* [0, n) */
int  rng_int(Env *e, int lo, int hi);       /* [lo, hi) — EXCLUSIVE, matches nextInt */
int  is_land(Env *e, int t);          /* bit7 */
int  is_ocean(Env *e, int t);         /* bit5 — water tiles only */
int  is_shoreline(Env *e, int t);     /* bit6 — set on land AND water */
int  magnitude(Env *e, int t);        /* bits0-4 */
int  terrain_type(Env *e, int t);     /* 0 plains, 1 highland, 2 mountain; land only */
int  is_shore(Env *e, int t);         /* land && shoreline */
int  is_ocean_shore(Env *e, int t);   /* land && any 4-neighbour ocean; computed live */
void heap_push(Env *e, Heap *h, int t, float p);  /* TILE FIRST, PRIORITY SECOND */
void update_border(Env *e, int t);
void conquer(Env *e, int p, int t);         /* the only territory mutation point */
void players_reset(Env *e);
float max_troops(Env *e, int p);
float start_troops(Env *e, int p);          /* reads is_bot */
void player_tick(Env *e, int p);
int  find_free_slot(Env *e);
void atk_push(Env *e, Attack *a, int t);
void attack_start(Env *e, int attacker, int target, float troops);
void dead_defender(Env *e, int attacker, int target);
void attack_tick(Env *e, Attack *a);
void annex_tick(Env *e, int p);             /* spec 9 driver, called after player_tick */
int  largest_incoming_attacker(Env *e, int p);
void bot_attack_random(Env *e, int p);
void bot_tick(Env *e, int p);
int  win_check(Env *e);
int  spawn_place(Env *e, int p);
void sim_init(Env *e, unsigned int seed);   /* zeroes the env; call once */
void sim_reset(Env *e);                     /* per-episode entry point */
int  sim_tick(Env *e);                      /* ONE tick; returns winner or 0 */
void sim_run(Env *e, int nticks);
```

**`heap_push` now takes `Env*` as well**, so it can count refusals at cap. Three same-type arguments in a row (`Env*`, `Heap*`, then `int, float`) — the tile/priority transposition trap is unchanged, and `conquer(e, p, t)` is still player-before-tile.

**Annexation scratch state** (all fields of `Env`, sized `N`; formerly file-static): `cl_visited`/`cl_gen`/`cl_stack`/`cl_comp`/`cl_start`/`cl_size` for border-component enumeration, and a **separate** `ff_visited`/`ff_gen`/`ff_stack`/`ff_take` for the territory fill. The two must not share a generation counter: `annex_remove` runs *inside* `annex_tick`'s component loop, so bumping one counter for both corrupts the enumeration mid-iteration. Both DFS stacks are bounded by `N` only because tiles are marked visited **before** push — move the mark after the push and both overflow.

**Player type:** `unsigned char is_bot[MAXP]`, all 1 until the agent seat flips one to 0. Four handicaps read it: `maxTroops/3`, growth `×0.5`, terra-nullius attacker loss `mag/10` vs `mag/5`, and the `mag *= 0.7` human-attacking-bot modifier (vs-player branch only; terra nullius is not a Bot).

**Annexation scheduling:** `long last_calc[MAXP]` (seeded with a per-seat offset so scans spread across ticks) and `long last_tile_change[MAXP]`, stamped for BOTH players inside `conquer`. `ticks` must be zeroed at the TOP of `sim_reset`, before spawn placement — otherwise spawn conquests stamp with the previous episode's counter and every player looks freshly-changed on tick 0.

**`alive` is a cached `tiles.count > 0`** (source's `isAlive()` is exactly that). Kept as a field only so `player_tick`/`bot_tick`/`annex_tick` can skip dead seats. Maintained in **both** directions inside `conquer` — clearing it on loss alone left it wrong, and annexation can empty a player without going through `attack_tick` at all.

Types: `terrain[N]` unsigned char, **packed byte** (bit7 land, bit6 shoreline, bit5 ocean, bits0–4 magnitude — see §3); `owner[N]` unsigned short; `TileSet {tiles[N], pos[N], count}` dense + reverse index, swap-and-pop, all O(1); `Player {TileSet tiles, border; float troops; int alive}`; `Heap {float pri[HEAPCAP]; int tile[HEAPCAP]; int count}` parallel-array min-heap; `Attack {int active, attacker, target; float troops; Heap heap}`.

**`HEAPCAP` is 2048, down from `8*N`.** `sizeof(Env)` 4.89 MB → 0.89 MB, which matters because PufferLib `calloc`s one `Env` per agent slot before it knows how many envs it needs. Verified behaviour-identical (byte-for-byte episode statistics against an `8*N` build) and performance-identical (0.12%, inside run-to-run spread): peak occupancy is 440 across 3000 episodes and no push is ever refused, and allocation size is not working set — only ~8 attack slots are ever active, so the touched footprint is ~24 KB either way. `heap_peak` and `heap_full_drops` are `Env` fields so a cap hit is loud rather than a silently truncated frontier. **440 is a scripted-bot floor, not a bound** — re-read `heap_peak` once a trained policy is sprawling.

Debug infra behind `#ifdef DEBUG` (release stubs `#define check_borders() ((void)0)` etc.): `ts_check`, `check_borders` (full border-invariant + phantom-tile scan), `run_tests()` = ts_test / conquer_test / blob_test (solid 12×12 rects — catches missing interior `ts_remove` in update_border) / hole_test (3×3 bite — catches missing neighbor-update loop in conquer) / heap_test (full-sort + random push/pop with heap-property scan) / annex_test (3×3 enclave inside a 12×12 gets annexed; two 12×12 blobs sharing one front do not — covers both directions of the bbox test) / attack_test / **isolation_test**.

**`isolation_test` is the one that guards the refactor.** Three envs run three full episodes each alone and are hashed; the same seeds are then rerun round-robin one tick at a time — the access pattern PufferLib's vec loop produces — and must hash identically. Any surviving shared array, counter, or generation stamp shows up here and nowhere else in the suite. Run it after any change touching env state.

`check_borders()` asserts the border invariant, phantom tiles, `alive == (tiles.count > 0)`, non-negative troops, and non-NaN troops. Extend it first whenever a new invariant is discovered — it is cheaper than the bug.

**Done & verified — v1 mechanics are complete.** Coordinate layer, terrain gen, TileSet, O(1) incremental border maintenance, `conquer`, min-heap, `Attack`, `attack_start` with cancellation and combination, `atk_push` with the real priority formula, real budget/combat math, `attack_tick`, troop growth, dead-defender wipe, elimination flag, spec §12 spawn placement, xorshift32 RNG, bot drivers with retaliation, annexation (§9 complete including the `isEnclosed` gate), win check, and the `Env` struct refactor.

Verification standard currently met, and the bar for any future change: clean under `-Wall -Wextra` both modes; ASan+UBSan clean over 300 episodes with `check_borders()` every 50 ticks, and over 20 episodes with it every *single* tick; deterministic across runs; 0 spawn failures; 0 heap drops; all seven tests pass.

**The binding is built and moved into the fork (Sept 2026), pending audit.** `openfront.h` now carries the PufferLib 5.0 interface on top of the sim: `puf_init` / `puf_reset` / `puf_step` / `puf_log` / `puf_render` / `puf_close`, plus `compute_observations`, `sorted_neighbors`, `apply_action`, `add_log`. `OBS_SIZE 31`, `ACT_SIZES {7}`, `NUM_ATNS 1`. Design decisions and measurements are in `openfront_project_reference.md` §5–6; this file stays the mechanics reference.

Two mechanics-adjacent points that belong here:

- **`apply_action` sends `troops / 5`** — spec §5's `attackAmount` default. There is no troop-commitment head, because §5 step 5 combination makes a repeat attack on the same target absorb the first, so action repeat supplies commitment. Combination is a prerequisite, not a fidelity nicety.
- **`annex_by[MAXP]`** was added to `Env`, stamped with the capturer inside `annex_remove`. It exists so the Log can attribute annexations to the agent seat rather than to all eight players — the diagnostic for whether the flat observation's inability to see geometry is a real ceiling.

**Not implemented, deliberately:** the spatial map observation and its custom encoder, action masking (`action_mask = NULL`; invalid neighbour slots fall through as noop), cities/gold, impassable terrain, exact per-attack frontier TileSets, retreat as an action index (§13, still a candidate), naval attacks, rivers. All Phase 2 or later.

**Real map gen is in progress (Sept 2026), procedural not real-map.** Decisions settled, do not relitigate:
- **Procedural simplex noise**, `simplex.h` vendored into `ocean/openfront/` from `ocean/battle/` (byte-identical copy is the upstream convention — battle, nmmo3 and terraform each carry one). Copy **battle's** octave wrapper, not terraform's: terraform initialises `max_value = FLT_MIN`, the smallest *positive* float, not a lower bound. Both versions contain a C99 VLA (`float frequencies[octaves]`) that C++ rejects — `cxxcheck.sh` fails on a verbatim copy, so use a fixed-size array.
- **Not real maps**, for two reasons: OpenFrontIO is AGPL-3.0 and shipping their map assets is a different act from deriving mechanics; and a real coastline downsampled to 2,304 tiles destroys exactly the straits and isthmuses that make real maps worth having. The clean later path is generating bins offline from public-domain geodata into `resources/openfront/*.bin` — in-repo precedent exists (`laser_puzzle`, `boxoban`, `tower_climb` all load offline-generated level banks).
- **The swap stays cheap only if the noise field never escapes the fill function.** One function fills `terrain[]` and nothing else; every later pass consumes the filled array. The real-map path supplies bit 7 from a `fread` and passes 3–5 run unchanged. Do not keep the noise field around for river sources, spawn scoring, or obs features — if elevation is needed later it is *in* `terrain[]`, which is the other reason the magnitude encoding had to land first.
- **Islands and lakes are kept**, not drowned or filled. Both would be workarounds for a boat-less v1 and both get torn out when naval lands. Spawn is restricted to the largest land component instead. Keeping lakes forces the §13 `isShore` / `isOceanShore` split in the same commit.
- **Land fraction 0.65**, quadratic edge falloff, one dominant continent. Higher than it looks like it needs to be: the radius-4 spawn disk requires every centre to sit ≥4 tiles from water, which a fractal coastline erodes far harder than the land fraction alone suggests.
- **Magnitude mapping takes no new knob.** `mag = clamp(round(30 * (noise - land_thresh) / (noise_max - land_thresh)), 0, 30)`. Land is the upper tail of a roughly Gaussian field, so its density decreases with elevation and a straight linear window gives majority-Plains for free — structurally the same as the generator's clamped `(Blue - 140) / 2`. Histogram the tier split before adding any parameter.
- **Dedicated `map_rng`**, seeded from a `map_seed` config key defaulting to a fixed nonzero value, so every env trains the same map and the comparison against 0.086 stays interpretable. Map gen must not consume `e->rng` — the procedural path uses a stream the real-map path will not, and episode determinism must not shift when that swaps. Per-episode maps become a config flip once spatial obs exists.
- **48x48 held** through the comparison run. Changing geometry and resolution together makes the result unattributable. See project reference §6.4 for what map size actually costs.

**Phase plan:** 0 = this prototype. 1 = **done pending audit** — action space, action repeat, and the PufferLib 5.0 binding (single header: **no `binding.c`, and no `<env>.c` either** — header-only envs are supported and upstream's `admiral` deleted its own `.c`), moved into `ocean/openfront/` Sept 2026 and building through `build.sh --cpu`. 2 = train it; reward/obs iteration, real map gen (procedural simplex, islands and lakes kept), spatial obs only if the annexation diagnostic justifies it. 3 = self-play harness, bots become held-out eval. 4 = raylib render, sweep, PR. Draft PR opens around end of Phase 2.

**Prior art:** `djmango/openfront-ai` — PPO self-play wrapping the real TS engine, ~2,100 game-ticks/sec after heavy optimization; needed a learned spatial autoencoder to compress obs. Two takeaways: strongest argument for the native C sim, and compress the map spatially / bypass exact scalars in obs design.

---


#### 28. Working conventions (cross-chat)

- **Claude writes the game simulation code; Sam audits it. Puffer binding setup is done together.** (Standing decision, Aug 2026 — this replaces the earlier convention where Sam wrote all core sim code.) The audit is the load-bearing half: Joseph reviews PRs on stream and asks implementation questions, so anything Sam cannot explain unprompted is not done. Annexation in particular — be able to explain why the cluster fill runs over the border set rather than the territory, and why the enemy bbox contains the cluster bbox rather than the reverse, without looking.
- Claude verifies any handed-over code compiles and passes in the container before handing it over.
- **Hand over whole functions, never excerpts.** Excerpt boundaries are exactly where adjacent required lines get dropped; this produced three clean-compiling defects in one pass (project reference §7). The same rule applies to these markdown files: replace whole sections, not lines.
- Recurring bug classes to watch: `=` vs `==`, `.` vs `->`, missing braces, missing return, struct-by-value vs pointer, Python-isms, nested function defs, **tile-number vs player-id confusion** (everything is a bare int: tiles are `t`/`nb[k]`/heap contents; players are `p`/`attacker`/`target`/`owner[...]`), and **`heap_push` arg transposition** (int/float convert silently both ways — the compiler will not catch it).
- Placeholder economics before real economics. One variable per experiment. Don't run phases ahead of where the code is.
- **A refactor is not verified until a one-variable control is bit-identical.** When a structural change lands alongside behavioural ones, revert the behavioural ones and diff against the pre-change build before layering them back one at a time. "Direction and rough magnitude match" is not verification; it is where a transposed argument hides. Note that a *stale global reference* is not the risk after a globals→struct move — the globals are gone, so it won't compile. The risk is a transposed argument or wrong index, which compiles clean and passes every invariant.
- **Don't reason about cache from `sizeof`.** A 5.5× reduction in `sizeof(Env)` produced 0.12% throughput change, because untouched `calloc` pages are never faulted in and the actual working set was ~24 KB either way. Measure the touched footprint, not the allocation.
- **Code that is never executed is not verified.** The standalone harness calls no `puf_*` function, so the whole binding compiled and proved nothing until `drive_test.c` existed to drive it — and that harness found two clean-compiling defects on its first run. Whenever a new interface is added, add the thing that exercises it in the same pass.
- Sam doesn't read TypeScript and doesn't need to — this spec is the interface to the source. Terse prose, no filler, settled things stay settled.

---

## 25 Sept 2026 — perf pass

**Bisect.** `of_fast bench` over every header commit from `d884e6a9` to `4a3d2848` (M3 Pro, `-O2 -ffp-contract=off`, today's `harness.c`, median of 3). Commits before map gen don't build against the current harness.

| Commit | ticks/sec | Change |
|---|---|---|
| `d884e6a9` … `c0ba9474` | BUILD-FAIL | pre-map-gen headers (5 commits) |
| `e26522b7` | 915018 | map gen |
| `1874b8ff` | 914978 | shore gate split |
| `48754b77` | 888134 | dead `is_land` guards |
| `61514968` | 810596 | DetMath (−8.7%) |
| `297cad49` | 814522 | tests xorshift |
| `eea12848` | 810198 | attack clamp |
| `39db150f` | 839683 | 2a double |
| `84ebc825` | 825647 | 2b `int64_t` |
| `5175a70e` | 835935 | spawn disk |
| `908b785a` | 835602 | 1a border set |
| `c4faca4a` | 752915 | 1b combat (−9.9%) |
| `9b542489` | 759183 | 4a capturer |
| `d6aef90c` | 749394 | 4b hole selection |
| `4a3d2848` | 749555 | anchor flip |

Noise is about ±3% at median-of-3 (compare the near-no-op steps); only DetMath and 1b clear it. End to end, `e26522b7` → `4a3d2848` is −18%. After the perf pass, `3e26237d` benches ~830k, about 9% under `e26522b7`: the remaining gap is the growth `det_pow` plus sub-noise steps.

**Commits** (both bit-identical: `of_dbg` stdout `cmp`-equal apart from the `sizeof(Env)` line; warning sets unchanged):

- `fa64934c` LTB table. `lt_sig[n] = lt_sigmoid(n) = det_sigmoid(det_log((double)n), 2.5, det_log(300000))`, n = 0..`OF_N`. The three bonuses (`lab` on attacker tiles with depth 0.7, `ldb` on defender tiles with 0.3, `lasb` on attacker tiles with 0.73) share k and m, so one table serves all three; `1.0 - depth * s` stays in `attack_logic`, which now takes `atk_sig` / `def_sig` and stays pure. `large_territory_bonus` was deleted rather than kept with a `double` argument, so an old int-taking call can't compile silently. The golden test calls `lt_sigmoid` directly (case 4 is n = 300000). Exhaustive old-vs-new check, n = 0..2304 × 3 depths: 0 / 6915 bit mismatches at `-O0` and `-O2`. `lt_sig[OF_N]` is 5.2e-6, so at 48×48 the bonus is within ~4e-6 of 1. `sizeof(Env)` 985352 → 1003800 (+18440 array, +8 padding).
- `3e26237d` troop-cap table. `cap_pow[n] = det_pow((double)n, 0.6)`, read by `max_troops` (all five callers go through it). Growth `det_pow(troops, 0.73)` is untouched, since troops are unbounded. Exhaustive check against the old `max_troops`, both bot flags: 0 / 4610. `sizeof(Env)` → 1022240. `of_dbg` stdout sha256 `0a87cd753ecc4e6eca039b8810b615cac4fc6c60961c852c88cbb5b77c6251ea`.

**Benches** (`of_fast bench`, interleaved A B, 5 each, all binaries built first; `BTLEServer` held one core at 100% throughout):

| Pair | Runs (ticks/sec) | Median |
|---|---|---|
| `4a3d2848` | 717900, 723648, 745300, 743399, 717128 | 723648 |
| `fa64934c` | 810572, 823042, 822451, 778780, 812954 | 812954 (+12.3%) |
| `fa64934c` | 656585, 798888, 778925, 802921, 809509 | 798888 |
| `3e26237d` | 805277, 774479, 828453, 837011, 857558 | 828453 (+3.7%) |

The second pair is noisy: `3e26237d` won 4 of 5, and `fa64934c`'s median drifted 813k → 799k between sessions. Call the troop-cap gain ~2–4%. The ~525k figure recorded at Tier A was not reproduced: `4a3d2848` gave 717–745k here.

**The `puf_init` catch.** The framework callocs `Env` and calls `puf_init`, never `sim_init`. `puf_init` can't call `sim_init`, because its memset would wipe `rng` and `agents[]`. Tables filled only in `sim_init` would have been all zeros in training: a flat 100000 troop cap and every LTB at 1.0. `of_dbg` would still have passed, since the harness only uses `sim_init`. Both inits now call `tables_init(e)`, and `drive_test` asserts the tables after `puf_init` (`80e82c6`). Negative control: with `tables_init` removed from `puf_init`, `drive` fails at n = 1.

**`drive` was broken.** From `e26522b7` (20 Sept, map gen), `puf_init` read `land_frac` and `map_seed` through `dict_get`, which exits on a missing key; `drive_test.c` never set them. `81b078ab` (21 Sept) added the keys to `config/openfront.ini`, which fixed the framework path but not `drive`, whose Dict is built by hand. So the binding path went unexercised from 20 Sept until `80e82c6`. During the perf pass it was checked with a scratch copy carrying the two keys: output was identical at `4a3d2848`, `fa64934c` and `3e26237d`.

## 25 Sept 2026 — B-lite decisions; `fronts` mode

Decided in chat: Discrete-13 (`build_city`, `post_nb0..4`) instead of the proposed Discrete-9. Env-side auto-targeting of posts was rejected because a 50-tick build lands after the attack it reacts to, and the front choice is the decision to learn. `goldMultiplier` 10, City bonus 25k/level (joins the start-troops/floor coupled group), upgrades out, bot gold base 50 as a fifth `is_bot` handicap. Spec §0 items 1, 4 and 5 are closed; 2 and 3 (post range, min-dist) wait on measurement.

Added `harness.c` `fronts <seed>` to measure front geometry. Finding: `sorted_neighbors` counts adjacent tile *pairs*, not distinct tiles (the longest front's p50 is 31 pairs vs 23 tiles); the neighbour sets agree. The mode uses `sorted_neighbors`' count as primary, reports both, and asserts agreement on the top 5 slots every sample. Read-only check: the `ep` lines for seeds 42 and 43 are `cmp`-equal to `hist`'s. `of_dbg` sha is unchanged (`0a87cd75…`), and all three `drive` configs are byte-identical to reference §3. Numbers are in reference §5.2.

Later on 25 Sept: range and min-dist were set from the `fronts` numbers. Defense post range is 30 → 6. Radius 7–8 would cover 64–83% of the median territory (242 tiles, by πr²), and at ×5 mag one post would shield nearly everything. Radius 6 covers ~47%, about half the longest front (p50 ext 9.8). Structure min-dist is 15 → 3, keeping upstream's range/min-dist ratio of 2. Tune range after the retrain if posts are ignored or dominant. On checking the header, there is no post-distance comparison yet: `has_post` is a bare `int` into `attack_logic`, passed as `0`. By the spec, the range should be inclusive (`d² <= r²`, §2.4) and min-dist strict (`d² < r²`, §15.5).

## 25 Sept 2026 — B-lite step 1: gold (`7b4e6d01`)

`int64_t gold` and `sent_attack` were added per player. Income is `floor(base × GOLD_MULT)` (base 50 bot / 100 human, multiplier 10), paid in `player_tick` after troop growth. `conquer_player_gold` runs at the start of `dead_defender` (every trigger) and in `annex_remove` when the collected set is the whole territory. A dead player's gold is zeroed in `player_tick`. That zeroing is what removes a passive human's gold, since the transfer skips them.

Upstream check at `7defd24`: the "never sent an attack" test is the `ATTACK_INDEX_SENT` **troop sum** (`GameImpl.ts:1389`), not a flag. It's recorded after `removeTroops`, before cancellation (`AttackExecution.ts:139`), and decremented only by a player-ordered retreat (`attackCancel`). Retreat isn't an action here, and our `attack_start` only reaches `troops_remove` with at least 1 troop, so a flag set there is equivalent. Revisit if retreat is ever added.

Verification: stdout with the `env` hashes, the `sizeof` line and the new `gold ok` line stripped is `cmp`-identical to `3e26237d`'s (318 lines). The new sha is `0a769dcf…` and `sizeof(Env)` is 1022312 (+72: `gold` adds 8 per player; `sent_attack` fills the 4 bytes of padding after `alive`. 9 players × 8). All three `drive` configs are byte-identical. x86_64 under Rosetta (`-O0` and `-O2` DEBUG) matches the Mac output byte for byte, and the Linux x86 bundle (both builds) matched.

## 25 Sept 2026 — B-lite step 2a: structures (`61a71536`)

City and DefensePost, with nothing issuing builds yet. The pieces:
- storage and costs (`min(unitsOwned, unitsConstructed)`)
- `build_structure` on a resolved tile
- capture in `player_tick` (step 2, before death)
- the City bonus inside `max_troops` before `/3`
- the post modifier at the `attack_logic` call site (completed posts, `d² ≤ 36`)
- bot scrapping and the §14.2 expand-ratio case

**Item 5:** the zero-city bonus term is `+0.0`, which is exact for `m > 0`. No special case was needed. **Item 7:** upstream's "owns structures" is `units().some(Structures.has)`, which includes under-construction and marked units.

The first version followed the brief's timing: `last_delete_tick` 0, delete at mark + 300, complete after D ticks. Reading upstream showed three off-by-ones, and the decision was to match upstream on all three:
- `lastDeleteUnitTick` starts at −1.
- Deletion when `ticks − (mark + 300) > 0`, i.e. mark + 301, with the mark applied at `DeleteUnitExecution.init`, end of tick.
- Construction checks for 0 before decrementing, so D + 1.

These moved into `structures_end_tick`, after the player loop, where upstream's later-added executions run. A bot's scrap is now a request resolved at end of tick: it's refused without spending the cooldown if the structure was captured that tick. The spec §14, §15.4 and §15.7 now carry exact timings with line refs. The amended commit replaced the unpushed `0bdc042b`.

Verification:
- **Mutation check.** 17 planted mutations; 16 caught.
  - The survivor is the end-of-tick cooldown recheck. It's an equivalent mutant: the request and its resolution are in the same tick, so the recheck can't fail. It's kept for fidelity.
  - A first pass let "end pass not wired into `sim_tick`" survive. A `sim_tick` construction case was added.
- **Behaviour.** Stdout, stripped of `env` hashes, `sizeof` and test lines, is `cmp`-identical to gold. New sha `60dc1afb…`, `sizeof(Env)` 1030808.
- **Drives.** All three are byte-identical.
- **x86.** Batched with commit 2: one bundle will cover both.
- **Warnings.** clang-22 `-Wconversion` gives the same warning set as `7b4e6d01` (23). A new unused-function warning for `build_structure` in the non-DEBUG builds lasts until commit 2.

