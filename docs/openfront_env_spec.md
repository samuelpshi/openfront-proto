# OpenFront C Environment — Mechanics Spec (full game)

Reference for the OpenFront reimplementation targeting PufferLib's Ocean suite. Read directly off `openfrontio/OpenFrontIO`. This edition **supersedes the `fc50009` edition in full**: that edition covered the territorial core only, and the core itself has drifted since (§1). If anything here looks off during implementation, re-clone and re-read the named file rather than reconstructing from memory:

```
git clone --depth 1 https://github.com/openfrontio/OpenFrontIO.git
```

**Source anchor: commit `7defd2483b160b41f45a8363d4453435f6832740`, 21 Sep 2026.** Previous anchor `fc50009` (18 Aug 2026). Diff against this hash before trusting any section. Upstream moves fast: between the two anchors `Config.ts` changed by ~520 lines and the combat model was rewritten.

**Reading rule, learned the hard way: read the signature, not the call site.** Same-type argument confusion produced two spec defects in Aug 2026 (`inscribed(outer, inner)`, `nextInt(min, max)`). Any call whose arguments share a type gets its declaration read.

**Scope of this edition.** Every system in `src/core/` that changes game state is specified here, with two exceptions called out explicitly: the Nation AI (§26 points to a separate file, `openfront_nation_ai_spec.md`, which is partial), and the rail network's routing internals (§20, summarised). Client-only and presentation systems are listed in §24 as not simulated.

**Files read for this edition** (all under `src/core/` unless noted): `configuration/Config.ts` (full); `execution/` — `AttackExecution`, `PlayerExecution`, `RetreatExecution`, `SpawnExecution`, `SpawnTimerExecution`, `TribeSpawner`, `TribeExecution`, `WinCheckExecution`, `ConstructionExecution`, `UpgradeStructureExecution`, `CityExecution`, `FactoryExecution`, `PortExecution`, `DefensePostExecution`, `MissileSiloExecution`, `TransportShipExecution`, `BoatRetreatExecution`, `WarshipExecution`, `MoveWarshipExecution`, `ShellExecution`, `TradeShipExecution`, `NukeExecution`, `MIRVExecution`, `SAMLauncherExecution`, `SAMMissileExecution`, `TrainStationExecution`, `TrainExecution`, `TargetPlayerExecution`, `EmbargoExecution`, `EmbargoAllExecution`, `DonateGoldExecution`, `DonateTroopExecution`, `DeleteUnitExecution`, `alliance/*` (request, extension, break), `NationExecution` (full), `utils/AiAttackBehavior` (bot-relevant paths, strategy table, troop sizing), `Util.ts`; `game/` — `GameImpl` (conquer, relinquish, borders, alliances, immunity, winner, conquerPlayer, tick loop), `PlayerImpl` (most), `AttackImpl`, `UnitImpl`, `GameMap`, `WaterManager` (outline), `TransportShipUtils`, `TrainStation`, `RailNetworkImpl` (connection rules); `pathfinding/` — `PathFinder` (water chain), `PathFinder.Air`, `PathFinder.Parabola`, `spatial/SpatialQuery`; `PseudoRandom.ts`, `Util.ts`, `DetMath.ts` (header); `map-generator/map_generator.go` (diff since `fc50009`).

**Not read in full, flagged where relevant:** `DoomsdayClockExecution` / `DoomsdayClock` (config constants only; the mode is deferred), the Nation AI behaviour files (**deferred**, §26), `WaterManager` magnitude/shoreline fixup body, the station-graph pathfinder (`StationPathFinder`), `StatsImpl` (stats are not simulated).

Licensing is settled: rules, mechanics and constants are not copyrightable expression; a C reimplementation from this spec does not trigger AGPL. A header comment crediting OpenFront.io is the whole mitigation. Do not relitigate. The discipline that matters grows with scope: **derive, don't transliterate** — hardest to hold across the Nation AI heuristics, where the temptation is to port control flow line by line.

---

## 0. Build order (tiers) and the PR #1 stopping point

Each tier is playable on its own and depends only on tiers above it.

| Tier | Content | Spec sections |
|---|---|---|
| A | Territorial core, re-derived to this anchor | §2–§14 |
| B | Gold, units, structures, construction, cities, defense posts | §15–§17 |
| C | Water components, transport ships, ports, trade ships, warships | §18–§19 |
| D | Silos, nukes, fallout, SAMs, MIRVs | §21 |
| E | Relations, alliances, traitor, embargo, donations, targeting | §22 |
| F | Factories, train stations, rails, trains | §20 |
| G | Nation AI | §26 + separate file |
| H | Teams, difficulty, anti-stall modes, ranked rules | §23 |

Tier A is complete (25 Sept 2026, `4a3d2848`); §25 lists what changed and what remains inert. Tier B-lite is implemented (25 Sept 2026): gold `7b4e6d01`, structures `61a71536`, build actions, placement, obs and log `e27e24f3`; see the project reference §1 and §4.

**PR #1 stopping point (decided 24 Sept 2026): Tier A + "Tier B-lite".** The full-game scope stands as the long-run plan, but PR #1 stops at the smallest build that contains the game's actual decision loop: spend troops on land, spend gold on troop capacity (City), or spend gold to hold a border cheaply (Defense Post). Without gold and structures the agent has one decision (which neighbour to hit), which is why the current build plays like territorial.io rather than OpenFront.

Tier B-lite is exactly:

| In PR #1 | Sections |
|---|---|
| Gold income; `conquerPlayer` gold transfer | §6.4, §5.3 |
| Costs, construction, placement, capture — City and DefensePost only | §15.2–§15.5, §15.7 |
| City troop capacity | §16 |
| Defense-post attack modifier (posts do not shoot at this anchor) | §17.1 |
| Bot structure scrapping (bots mark captured structures for deletion) | §14 step 2, §15.7 |

Out of PR #1: upgrades (§15.6, decided out 25 Sept — see below), every other unit type, and Tiers C–H. Later PRs, in likely order: nukes (Tier D — the spectacle, second PR), then naval (C) and rail/Factories (F) together with real-scale maps, where water pathing and 110-tile rail geometry mean something. At 48×48 every City would sit within station range of every Factory and the rail network would be degenerate. Diplomacy (E) only makes sense with self-play.

**Open decisions for Tier B-lite (record the answers in the project reference):**

1. **Answered 25 Sept** (goldMultiplier 10; see the project reference §4 rescale table). **Economy vs episode length.** A human earns 100 gold/tick; the first City costs 125k = 1,250 ticks against a ~1,900-tick episode. At upstream prices structures barely occur. Scale costs or income, or lengthen episodes — a documented rescale either way (§24.1).
2. **Answered 25 Sept** (6; see the project reference §4 rescale table). **Defense-post radius.** 30 tiles covers most of a 48×48 map. Rescale with the map, like the wipe threshold.
3. **Answered 25 Sept** (3; see the project reference §4 rescale table). **Structure min-distance** (15, §15.5) — same question, smaller stakes.
4. **Answered 25 Sept** (Discrete-13 with per-neighbour post targeting; see the project reference §4). **Action space.** Discrete-7 → Discrete-9 (`build_city`, `build_post`) with automatic placement: City on the deepest interior tile, post on the border facing the most dangerous neighbour. Extends the settled Discrete-7 decision; does not reopen it.
5. **Answered 25 Sept** (out; see the project reference §4). **Upgrades.** Probably out (a second City is the same decision as a City upgrade); confirm.

---

## 1. Changes since the `fc50009` edition

All sixteen corrections (upstream drift since `fc50009`, plus rules the previous edition missed) are folded into the sections below, and the Tier A ones are implemented (§25). The itemised list is archived in `docs/history.md`. When upstream moves again, diff against the §-anchor commit and record new drift here until it is folded in.

---

## 2. Numeric model

### 2.1 Types

| Quantity | Source type | Semantics |
|---|---|---|
| Player troops | `bigint` | Every write floors: `addTroops(x)` adds `floor(x)`; `removeTroops(x)` removes `min(troops, floor(x))` and returns the amount removed; `addTroops(negative)` calls `removeTroops(−x)`. `setTroops` floors. |
| Attack troops | `number` (double) | `setTroops` clamps at 0. Not floored. |
| Unit troops (transport) | `number` | Not floored on the unit; floored when removed from/returned to the player. |
| Gold | `bigint` | Integer throughout; `removeGold` clamps to available and returns the amount removed. |
| Unit health | `bigint` | `modifyHealth(d)`: `clamp(health + floor(d), 0, floor(maxHealth))`; reaching 0 deletes the unit. Units without `maxHealth` have health 1. |
| Relations | `number` | Clamped to [−100, 100]. |

All other arithmetic is IEEE double. **In C:** sim math is `double`; player troops are `int64_t` behind `troops_set` / `troops_add` / `troops_remove`, which floor with upstream's semantics; attack troops are `double`, clamped at 0 on every write; heap priorities stay `float` (every key value is exact in float). Gold (B-lite) should be `int64_t` for the same reason.

### 2.2 Deterministic transcendental math (`DetMath.ts`)

Upstream replaced `Math.exp`, `Math.log`, `Math.pow`, `Math.atan2` in `src/core` with implementations that use only `+ − × /` and IEEE bit views, because engines differ in the last bit and a one-bit difference at a truncation boundary (`floor` on troops, gold, the nuke edge) desyncs clients. Accuracy ~1e-8 relative; negative `pow` bases unsupported. `Config.ts` imports `exp, log, pow, pow2` from it.

**Done (Tier A #1).** `DetMath` is ported to C in `double` (`det_exp/det_log/det_pow/det_pow2/det_atan2`); no libm transcendental remains in the header. FMA contraction is off via `#pragma STDC FP_CONTRACT OFF` at the top of `openfront.h` (restored at the bottom), not a `build.sh` flag. Since #2a (double sim math) the pragma is demonstrably load-bearing: stripping it with `-ffp-contract=fast` changes `hist_run` output on arm64. Integer truncation of troops (§2.1) makes this load-bearing rather than cosmetic.

Verified: `of_dbg` stdout is byte-identical between Mac arm64 (Apple clang 15) and x86_64 (clang 18), under both `-O0 -ffp-contract=off` + ASan/UBSan and `-O2 -mavx2 -mfma`. Note for implementers: Apple clang's `-Wfloat-conversion` does not flag double→float narrowing; `-Wimplicit-float-conversion` does, and both are in `mk.sh`/`cxxcheck.sh`.

### 2.3 Random numbers

`PseudoRandom(seed)`: a 4×32-bit state (`s0..s3`) seeded by a splitmix-style `split()` of `seed | 0`, warmed up 12 steps; `next()` returns `t/2^32` in [0,1). Helpers: `nextInt(lo, hi)` = `floor(next()·(hi−lo)) + lo` — **max exclusive**; `chance(n)` = `nextInt(0, n) === 0` (probability 1/n); `randElement`, `shuffleArray` (Fisher–Yates, `j = nextInt(0, i+1)`), `nextID()` (base-36 string).

Streams are per-object, seeded from ids, hashes or the current tick: attacks `123` (fixed); bots `simpleHash(id)`; nations `simpleHash(id)+simpleHash(gameID)`; spawns `simpleHash(playerId)+simpleHash(gameID)`; ports, warships, shells, SAMs, nukes, trains `tick` at init (or unit id). **Reproducing upstream's streams is not a goal.** The C sim keeps its own seeded RNG (`e->rng`, xorshift32) and must only preserve the *distributions* (the range and exclusivity of each draw). `rng_int(lo, hi)` is max-exclusive to match `nextInt` so arguments transcribe verbatim.

`simpleHash(s)`: Java-style `h = (h<<5) − h + c` over char codes, 32-bit, `abs` at the end.

### 2.4 Helpers

`within(v, lo, hi)` = clamp. `sigmoid(v, k, m) = 1 / (1 + exp(−k·(v − m)))` (DetMath `exp`). `getMode(map)` returns the key with the strictly largest count, first in insertion order on ties. Distances: `manhattanDist`, `euclideanDistSquared` on tile coordinates; `nearbyUnits` / `hasUnitNearby` use **Euclidean, inclusive** (`d² ≤ r²`).

---

## 3. Tick & execution model

- `msPerTick = 100` → 10 ticks per second of game time. All rates are per tick.
- The engine holds a list of **executions**. Each tick: every initialised, active execution ticks in list (insertion) order, skipping those not `activeDuringSpawnPhase()` while in the spawn phase; then executions added during the tick are **initialised at the end of the tick** and act from the next tick; inactive executions are removed. Consequence: anything added this tick (an attack, a nuke, a boat) first acts next tick.
- Then the water manager flushes pending land→water conversions (water-nuke mode only, §21.5) and the tick counter increments.
- **Spawn phase** lasts while `startTick === null`. `SpawnTimerExecution` ends it once `ticks > numSpawnPhaseTurns` (100 singleplayer / 150 random-spawn / 200 multiplayer); in singleplayer it also ends the moment the human spawns. Almost every gameplay execution is inactive during the spawn phase (attacks, growth, bots, structures, win check is not gated but has nothing to see).
- `ticksSinceStart` = ticks since the spawn phase ended; `elapsedGameSeconds = ticksSinceStart / 10`.

**Equivalent fixed order for the C main loop.** Upstream order is "insertion order of executions", which interleaves per-player executions with attack and unit executions by creation time. Any fixed order is acceptable as long as it stays fixed; the recommended order is:

1. Bot and nation decisions (each on its own phase-offset schedule, §14, §26)
2. `attack_tick` for every active attack, in creation order
3. Unit executions (ships, shells, nukes, SAM missiles, trains, structures), in creation order
4. Per player (`PlayerExecution`, §6.1): relations, structure capture, death, growth, gold, alliance and embargo expiry, annexation
5. Every 10 ticks: win check (§12)

---

## 4. Map representation

- `uint8 terrain[]`: bit7 land, bit6 shoreline, bit5 ocean, bits0–4 magnitude 0–31. Mutable only through nuke water conversion (§21.5).
- **Magnitude 31 on a land tile = impassable**: solid ground that cannot be owned, attacked, nuked, or crossed by nuke trajectories. Excluded from `numLandTiles`.
- Terrain type derived: land with mag ≥ 31 → Impassable; < 10 → Plains; < 20 → Highland; else Mountain. Water → Ocean.
- Magnitude semantics: land magnitude is elevation; water magnitude is `ceil(manhattan distance to land / 2)` capped at 31 (authority: `packTerrain` in `map_generator.go`; `WaterManager` recomputes water magnitude after conversions).
- `uint16 state[]`: bits0–11 owner smallID (0 = terra nullius, max 4095), bit13 fallout, bit14 defense bonus (unused by any rule at this anchor), bit15 reserved.
- Shoreline bit is set on land **and** water tiles whose land-ness differs from some 4-neighbour. `isShore(t)` = land ∧ shoreline. `isOceanShore(t)` = land ∧ some 4-neighbour has the ocean bit (computed live, not a bit).
- `TileRef = y·W + x`. **4-neighbour order everywhere is N, S, W, E** (`−W, +W, −1, +1`). 8-neighbour order (`neighbors8`): `x−1` column (N,·,S), then N, S, then `x+1` column (N,·,S).
- `isBorder(t)`: any 4-neighbour has a different owner id (water has owner 0, so owned coastal tiles are border tiles).
- **`isOnEdgeOfMap(t)`: on the map boundary, or any 4-neighbour is impassable.** (Changed since `fc50009`.)
- `numLandTiles` counts passable land; `setWater` decrements it.
- `setWater(t)` (water-nuke mode only): terrain byte becomes `0` — lake water, no ocean bit, no shoreline, magnitude 0 — and the water manager then propagates the ocean bit from ocean neighbours and recomputes magnitudes and shorelines around the crater.

The C encoding (`OF_LAND_BIT 0x80`, `OF_SHORE_BIT 0x40`, `OF_OCEAN_BIT 0x20`, `OF_MAG_MASK 0x1F`) already matches. Never compare `terrain[t]` directly — use the accessors.

---

## 5. Territory, borders and neighbours

### 5.1 `conquer(owner, tile)`

1. Throw if water or impassable (C: debug trap; the invariant owned ⇒ passable land must hold by construction).
2. If previously owned: stamp `lastTileChange = ticks` and bump `tileChangeVersion` on the previous owner; remove the tile from its `tiles` and `borderTiles`.
3. Set owner; add to the new owner's `tiles`; stamp `lastTileChange` and bump version on the new owner.
4. `updateBorders(tile)`: recompute border membership for `tile` and its 4 neighbours (unowned tiles are skipped; owned tiles are added to their owner's border set iff `isBorder`).
5. **Clear fallout on the tile.**

### 5.2 `relinquish(tile)`

Owned land → unowned: stamp and bump the previous owner, remove from its `tiles` and `borderTiles`, set owner 0, `updateBorders(tile)`. Used by nukes (§21) and by spawn re-placement (§13).

### 5.3 `conquerPlayer(conqueror, conquered)` — the "player eliminated" hook

Called by the dead-defender wipe (§10) and by annexation when a cluster is the victim's entire territory (§11). It does **not** move tiles itself. Effects:

- If `conquered` is a disconnected teammate of `conqueror`: its warships and transport ships are captured.
- **Gold:** skipped entirely if `conquered` is a Human who never sent an attack. Otherwise `conqueror` gains `conquerGoldAmount` = all of `conquered`'s gold if it is a Bot or Nation, half if Human; and `conquered` loses **all** its gold regardless.

### 5.4 `nearby()` — the neighbour list used by AI targeting

The set of owners (players and terra nullius) that are:
- 4-adjacent to any of the player's border tiles, restricted to passable land, excluding unowned tiles that carry fallout; plus
- **shore-reachable across a river**: for every 10th shore tile in the player's border set (by border-set order), for each cardinal direction whose first step is water, the tile 5 steps out, if it is passable land and not unowned-with-fallout.

Excludes the player itself. Memoised on territory and water versions. Distinct from `sharesBorderWith(other)` (a strict 4-adjacency test over border tiles), which decides land vs boat attacks (§14).

### 5.5 `canAttack(tile)` (human intent validation)

Not own tile; if owned by a player, `canAttackPlayer` (§13.4); passable land; if owned: `sharesBorderWith(owner)`; if unowned: some tile reachable from it through unowned passable land within Manhattan 200 is 4-adjacent to the player.

---

## 6. Players and economy

### 6.1 `PlayerExecution.tick` — per player, per tick, in this order

Inactive during the spawn phase. `lastCalc` is initialised at `tick + simpleHash(playerId) % 20`.

1. **Relation decay** (§22.1).
2. **Structure capture.** For each of the player's structures (City, DefensePost, SAMLauncher, MissileSilo, Port, Factory): if the tile's owner is not a player → delete the structure; if it is another player → a DefensePost is deleted (destroyer = new owner), anything else is **captured** (ownership transfers, level kept).
3. **Death.** If the player owns 0 tiles: delete all gold; delete every unit except nukes in flight (AtomBomb, HydrogenBomb, MIRV, MIRVWarhead); silently remove all alliances; deactivate. Stop.
4. **Troop growth**: `addTroops(troopIncreaseRate)` (§6.3).
5. **Gold income**: `addGold(goldAdditionRate)` (§6.4).
6. **Alliance expiry**: any alliance with `expiresAt ≤ ticks` expires (§22.2).
7. **Temporary embargo expiry**: temporary embargoes older than 3000 ticks are removed (§22.4).
8. **Annexation** (§11), if `(ticks − lastCalc > 20 ∨ numTilesOwned < 100) ∧ lastTileChange ≥ lastCalc`; then `lastCalc = ticks`.

### 6.2 Player types

`Human`, `Nation` (named AI with a map-defined spawn cell, runs the Nation AI), `Bot` (tribe; runs `TribeExecution`). The RL agent seat corresponds to Human. Difficulty (`Easy`, `Medium`, `Hard`, `Impossible`) applies to Nations and to some Human-facing thresholds.

### 6.3 Troops

```
startTroops   = Bot 10_000 | Nation {Easy 12_500, Medium 18_750, Hard 25_000, Impossible 31_250}
                | Human 25_000   (1_000_000 with infinite-troops host cheat)
maxTroops     = 2·(tiles^0.6 · 1000 + 50_000) + Σ(level of built cities)·250_000
                  Bot: /3   Nation: ×{0.5, 0.75, 1, 1.25}   Human: ×1
toAdd         = (10 + troops^0.73 / 4) · (1 − troops/maxTroops)
                  Bot: ×0.5   Nation: ×{0.9, 0.95, 1, 1.05}
increase      = min(troops + toAdd, maxTroops) − troops        // may be negative → decay
```

`1 − troops/max` goes negative when max shrinks below current troops (territory loss); troops then decay toward the new cap. Keep it; it is load-bearing. Cities under construction do not count. `pow` is DetMath.

### 6.4 Gold

```
goldAdditionRate = floor(base · goldMultiplier)     base: Bot 50, else 100     // per tick
startingGold     = 0 by default (config); Bots always 0
```

Gold sources: this income, trade ships (§19.3), trains (§20.3), conquest (§5.3), piracy (§19.3), donations (§22.5). Gold sinks: unit costs (§15.3), donations. Cities produce troops capacity, **not gold**.

### 6.5 `isFriendly(a, b)`

`a === b`, or (`b` not disconnected, or AFK-friendly requested) ∧ (same team ∨ allied). Bots are on no team for friendliness purposes.

---

## 7. Attack lifecycle (`AttackExecution`)

State per attack: `{ attacker, target (player or TN), troops (double), sourceTile (null for land attacks, landing tile for boat attacks), border set (the attack's own frontier), retreating, retreated }`, plus a min-heap of candidate tiles and a private RNG (§2.3). A player's outgoing attacks are a list; a target's incoming attacks are a list filtered to attackers still alive.

### 7.1 Init (runs the tick after creation)

In this order; any "reject" deactivates the execution:

1. Target must exist; attacker ≠ target; reject if attacker `isFriendly(target)`.
2. If target is a player and **neither side is a Bot**: target adds a **temporary embargo** against the attacker (§22.4), and any pending alliance request *from the target to the attacker* is rejected. (This happens even if step 3 then rejects.)
3. If target is a player and `!attacker.canAttackPlayer(target)` (spawn immunity, §13.4) → reject.
4. `startTroops` defaults to `attackAmount` = `troops/20` if the attacker is a Bot, else `troops/5`. Every AI caller passes an explicit amount; the agent's `apply_action` sends `troops/5` (project decision, see project reference §4).
5. If `removeTroops` (true except for boat landings): `startTroops = attacker.removeTroops(min(attacker.troops, startTroops))` — **the floored amount actually removed**.
6. Create the attack. Seed the heap: boat landing → `addNeighbors(sourceTile)` only; land attack → `refreshToConquer()` (clear heap and border set, then `addNeighbors` for every attacker border tile).
7. **Cancellation.** For each of the attacker's incoming attacks whose attacker is this target: if `incoming.troops > attack.troops` → `incoming.troops −= attack.troops`, this attack is deleted, stop; else `attack.troops −= incoming.troops` and the incoming attack is deleted.
8. **Combination.** If this attack is a land attack (`sourceTile === null`), every other outgoing attack of the attacker against the same target — land *or boat-landed* — is absorbed: `attack.troops += other.troops`, other deleted. Boat-landed attacks never absorb anything.
9. If the target is a player: target's relation toward the attacker changes by −60 / −70 / −80 / −100 for Easy / Medium / Hard / Impossible (§22.1).

### 7.2 Candidate enqueue (`addNeighbors(tile)`)

For each 4-neighbour `n` of `tile` (N, S, W, E) that is passable land owned by the target: add `n` to the attack border set (deduplicated), count `numOwnedByMe` = attacker-owned 4-neighbours of `n`, and enqueue `n` with the priority in §8. Duplicates in the heap are expected and die in the validity checks.

### 7.3 Per tick

In order:
1. If `retreated` → retreat with 25% malus if the target is a player, 0% vs terra nullius (§7.4). Stop.
2. If `retreating` → do nothing this tick (frozen). Stop.
3. If the attack was deleted → deactivate.
4. If the target is a player now friendly (alliance formed mid-attack) → retreat with 0% malus.
5. `borderSize = attack.borderSize + rng_int(0, 5)`, fixed for the tick. `troopCount` = current attack troops.
6. `tickBudget = 1`. While `tickBudget > 0`:
   - `troopCount < 1` → delete the attack, **nothing returned**. Stop.
   - Heap empty → `refreshToConquer()`, then retreat with 0% malus (survivors returned). Stop. (The refresh is vestigial.)
   - Pop the tile; remove it from the attack border set.
   - Skip (no budget consumed) if no 4-neighbour is attacker-owned, if its owner is not the target, or if it is water/impassable.
   - `addNeighbors(tile)` (before the conquest).
   - `{attackerLoss, defenderLoss, tickFraction} = attackLogic(...)` (§9), with `attackTroops = troopCount`, live defender troops and tile counts.
   - `tickBudget −= tickFraction`; `troopCount −= attackerLoss`; `attack.troops = troopCount` (clamped ≥ 0); if the target is a player, `target.removeTroops(defenderLoss)` (floored, clamped to available).
   - `conquer(attacker, tile)`; then the dead-defender check (§10).

**C guard:** `borderSize` can be 0 (empty attack border and a 0 jitter draw). JS then divides by zero, `tickFraction = Infinity`, and exactly one tile is taken. Reproduce that explicitly: if `borderSize == 0`, take one tile and end the tick.

### 7.4 Retreat

`retreat(malus%)`: `deaths = troops · malus/100`; survivors `troops − deaths` go back to the attacker (`addTroops`, floored); attack deleted. Special case: a land attack created with `removeTroops = false` subtracts its `startTroops` first, so troops never paid are not refunded.

**Manual retreat (`RetreatExecution`)** is a two-step, delayed order: on its first tick it sets `retreating` on the attack, which freezes it (§7.3 step 2 — no conquests, but it still exists, still counts as an incoming attack, can still be cancelled by a counter-attack and still takes nuke losses). **20 ticks** after the order was issued it sets `retreated`; on the attack's next tick the survivors come home with the 25% malus (vs a player) or none (vs terra nullius).

Boat retreat is separate (§19.2).

---

## 8. Conquest priority (min-heap, lower pops first)

Computed at enqueue time:

```
mag      = 1 (Plains) | 1.5 (Highland) | 2 (Mountain) | 0 (anything else)
priority = (rng_int(0, 7) + 10) · (1 − numOwnedByMe·0.5 + mag/2) + tickNow
```

`rng_int(0, 7)` yields 0..6. `numOwnedByMe ≥ 2` on Plains makes the multiplier ≤ 0, so pockets and concavities fill first. `+ tickNow` makes older enqueues win across ticks. Unchanged since `fc50009`; the C `atk_push` matches. Heap: `FlatBinaryHeap`, unchanged.

---

## 9. Combat math (`Config.attackLogic`)

Pure function of `{terrain, attackTroops, attacker{type, numTiles}, defender{type, numTiles, troops, isTraitor, isDisconnectedTeammate} | null, defenderHasDefensePost, falloutRatio | null, borderSize}`.

**Terrain base:** Plains `mag 80, tileCost 16.5`; Highland `100, 20`; Mountain `120, 25`; Impassable throws.

**Modifiers applied first, to both branches where stated:**
- Defense post (defender is a player with an active, completed DefensePost within Euclidean 30 of the tile): `mag ×5`, `tileCost ×3`.
- Fallout on the tile: `f = 5 − 2·(numTilesWithFallout / numLandTiles)`; `mag ×f`, `tileCost ×f`. (Fallout tiles are unowned, so this bites attacks on terra nullius.)

**vs terra nullius** (`defender = null`):
```
attackerLoss = mag / (attacker is Bot ? 10 : 5)
defenderLoss = 0
tickFraction = within(2000 · tileCost / attackTroops, 5, 100) / (2 · borderSize)
```
The lower clamp 5 binds whenever `attackTroops ≥ 400·tileCost` (6,600 on Plains), so expansion speed is then pure frontier arithmetic: at most `0.4 · borderSize` Plains tiles per tick.

**vs a player:**
```
if defender.isDisconnectedTeammate: mag = 0
if attacker ∈ {Human, Nation} and defender is Bot: mag ×0.7

LTB(n, depth)      = 1 − depth · sigmoid(ln n, 2.5, ln 300_000)      // log-logistic, ≈1 small, →1−depth huge
largeAttackerBonus = LTB(attacker.numTiles, 0.7)
largeDefenderBonus = LTB(defender.numTiles, 0.3)
largeAtkSpeedBonus = LTB(attacker.numTiles, 0.73)
traitorLoss        = defender.isTraitor ? 0.5 : 1
traitorCost        = defender.isTraitor ? 0.8 : 1

defenderLoss  = defender.troops / defender.numTiles
ratio         = defender.troops / attackTroops
attackerLoss  = mag · traitorLoss · within(ratio, 0.6, 2)
                · (0.463 · largeAttackerBonus · largeDefenderBonus + 0.0039 · defenderLoss)
speedCost     = within(ratio, 0.82, 7.5) · within(ratio/20, 1, 50) / 8.55
tickFraction  = speedCost · tileCost · largeAtkSpeedBonus · largeDefenderBonus · traitorCost / borderSize
```

`sigmoid(ln n, 2.5, ln M) = 1 / (1 + (n/M)^−2.5)`. At 48×48 every territory bonus is ≈1 (`n/300k ≤ 0.008` → sigmoid < 6e-6). Fastest possible vs-player pace on Plains with no modifiers: `tickFraction ≥ 0.82·16.5/8.55/borderSize ≈ 1.58/borderSize`, i.e. at most `0.63·borderSize` tiles per tick. Losses rise with the defender's troop density (`0.0039 · troops/tile`) — packed land is expensive, thin land cheap.

**Guard in C:** `defender.numTiles` is ≥ 1 whenever a defender tile is being taken; `attackTroops ≥ 1` is guaranteed by the loop check. No other division needs a guard.

---

## 10. Dead-defender wipe

After every conquest in `attack_tick`, if the target is a player with `numTilesOwned < 100`:
1. `conquerPlayer(attacker, target)` (§5.3) — **on every trigger**, not once.
2. Up to 100 passes: for each tile still owned by the target (iterating the live set), if any 4-neighbour is attacker-owned → the attacker conquers it; else if any 4-neighbour is owned by a player that is neither the target nor **friendly to the target** → that player conquers it (first in N, S, W, E order). Stop early on a pass with no progress.

Threshold 100 is map-scale-bound (§24).

---

## 11. Annexation (`PlayerExecution.removeClusters`)

Scheduling: §6.1 step 8. The `lastTileChange ≥ lastCalc` guard is the cheap-out; copy it.

**Clusters** = connected components of the player's **border set** under **8-connectivity**, each with its bounding box. Membership is stamped into a generation-counted scratch array; floods use an explicit stack.

**Selection:**
1. 0 clusters → nothing.
2. **1 cluster (fast path)** → if `surroundedBySamePlayer(c)` returns a non-friendly enemy → `removeCluster(c)`. Done.
3. Otherwise the largest cluster by tile count (first on ties) is the candidate. **Hole check:** if some other cluster `j`'s bbox contains the candidate's bbox, flood the player's own tiles (8-connectivity) from one of them to decide whether the two clusters belong to the same contiguous territory; if they do, the candidate is a *hole* (the rim of a crater or enclave inside a larger territory whose outer perimeter is `j`). In that case pick instead the largest cluster that is not a hole by the same test. Territory ids are assigned lazily and cached for the call.
4. Largest cluster: `surroundedBySamePlayer` → `removeCluster` if the enemy is not friendly.
5. Every other cluster: `isSurrounded` → `removeCluster`.

**The bounding-box test — unchanged and still the easiest thing to get backwards.** The ENEMY neighbour bbox must CONTAIN the cluster bbox: `eminx ≤ cminx ∧ eminy ≤ cminy ∧ emaxx ≥ cmaxx ∧ emaxy ≥ cmaxy`. A ring enclosing a cluster always extends one tile past it; shared fronts never satisfy it.

**`surroundedBySamePlayer(cluster)`** (largest cluster): fail if any cluster tile is ocean-shore or on the map edge (edge now includes impassable-adjacent); fail on any unowned 4-neighbour; collect distinct non-self owners among 4-neighbours and **fail as soon as that set's size ≠ 1 after any tile**; finally require the enemy bbox to contain the cluster bbox. Returns the enemy.

**`isSurrounded(cluster)`** (other clusters): fail if any tile is shore (lake or ocean) or on the map edge; unowned neighbours do not block; require at least one enemy neighbour and enemy bbox ⊇ cluster bbox.

**`removeCluster(cluster)`**, in order:
1. Bail if any cluster tile is no longer owned by the player (an earlier removal this tick moved it).
2. Capturer: among non-friendly players 4-adjacent to the cluster, the one with the largest ongoing attack against this player; failing that the one bordering the cluster on the most adjacencies (`getMode`). None → bail.
3. **`isEnclosed(firstTile)`**: DFS from the cluster's first tile through the player's own tiles and unclaimed land (4-connectivity); reaching the map edge (incl. impassable-adjacent) or unowned water → not enclosed → bail. Other players' tiles are walls.
4. Collect the player's own territory reachable from the first tile (4-connectivity) — **collect first, conquer second**.
5. If that is the player's entire territory → `conquerPlayer(capturer, player)`.
6. The capturer conquers every collected tile.

---

## 12. Win condition (`WinCheckExecution`, every 10 ticks)

**FFA:** sort players by tiles owned; the top player wins if any of:
- `topTiles · 100 > (numLandTiles − numTilesWithFallout) · pct(elapsedSeconds)`, where `pct` = 80, or with overtime enabled `max(0, 80 − floor((floor(t) − 1800)·2/60))` after minute 30;
- a configured `maxTimerValue` (minutes) has elapsed;
- the hard limit of **170 minutes** (102,000 ticks after spawn phase) has elapsed.

**Team mode:** same test on summed team tiles; the Bot team can never win. Ranked 1v1/2v2 have extra last-human-standing rules and a cancel-if-not-spawned check (not simulated).

For the env: `numTilesOwned / (numLandTiles − fallout)` is the natural `perf`; 80% is the terminal; an agent also terminates at 0 tiles. The env's `max_steps` cap is an RL construct, not a source rule.

---

## 13. Spawning

### 13.1 Phase

Spawn phase: §3. During it, spawn executions run; humans may re-pick (previous tiles are relinquished and re-conquered if the new pick fails). Players get their start troops at creation (§6.3).

### 13.2 The spawn disk

`getSpawnTiles(center)`: 4-connected BFS from `center` over tiles satisfying `(dx + 0.5)² + (dy + 0.5)² ≤ 16`, where `dx, dy` are offsets from the centre — the Euclidean-4 disk measured from the corner shared by four pixels. **52 tiles.** Invalid tiles are owned, water or impassable. `requireAllValid = true` rejects the centre if any disk tile is invalid; `false` returns only the valid ones.

### 13.3 Placement

- **Chosen tile** (human intent, positioned tribes): take the valid subset of the disk; empty → fail.
- **Random** (bots, nations without a cell, random-spawn mode): up to 1000 tries; pick a uniform tile (or within the team spawn area); reject if water, owned, or a border tile; for tries ≤ 750 also reject if within Manhattan `minDistanceBetweenPlayers = 30` of any other player's spawn centre; require the full disk valid.
- **Nations** with a spawn cell: up to 50 tries of a uniform tile within ±25 of the cell (max exclusive) that is unowned passable land, rejecting Mountain with probability 1/2; failing that, no spawn this attempt. Nations outside their team's spawn area fall back to random placement.
- On success: conquer every disk tile, set the spawn tile, and on first spawn register the player's `PlayerExecution` (and `TribeExecution` for bots).

### 13.4 Spawn immunity

`isSpawnImmunityActive` = in spawn phase ∨ `ticksSinceStart < spawnImmunityDuration` (default **50 ticks**, configurable). Nations use a fixed 50-tick window. `player.isImmune()`: Human → spawn immunity; Nation → nation immunity; Bot → never. `canAttackPlayer(target)`: **only Human attackers respect immunity**; everyone respects friendliness. Nukes cannot be launched at all while spawn immunity is active (§21.2).

---

## 14. Bot driver (`TribeExecution` + the bot paths of `AiAttackBehavior`)

Per-bot constants from `PseudoRandom(simpleHash(botId))`: `attackRate = rng_int(40, 80)`, `attackTick = rng_int(0, attackRate)`, `triggerRatio = rng_int(50, 60)/100`, `reserveRatio = rng_int(30, 40)/100`, `expandRatio = rng_int(10, 20)/100`. `neighborsTerraNullius` starts true.

Acts only on ticks where `ticks % attackRate == attackTick`; deactivates when dead.

**First decision:** `sendAttack(terraNullius)` (not forced; §14.2). Done.

**Every later decision, in order:**
1. Accept every incoming alliance request; for each alliance where exactly one side has asked to extend, agree to extend (§22.2).
2. If the delete cooldown allows (§15.7), mark the first structure it owns for deletion — bots scrap structures they capture. Only structure types count (`Structures`, `Game.ts:233`), and ones already marked are skipped (`TribeExecution.ts:86-93`). Under-construction structures are eligible. The **first decision** is the TN attack alone and returns before this step (`TribeExecution.ts:58`). Deciding only *adds* a `DeleteUnitExecution`. The mark, and the refusal checks, happen at that execution's `init` at the end of the tick (§3, §15.7).
3. **Traitor punish:** pick a random non-friendly traitor among `nearby()` (none if alliances are disabled). With probability 1/3 (1/6 if currently friendly with it — breaking the alliance first) → `sendAttack(traitor)`; stop if sent.
4. While `neighborsTerraNullius`: if any entry of `nearby()` is terra nullius → `sendAttack(TN)`, stop if sent; else clear the flag permanently. Falls through on failure.
5. `attackRandomTarget()`:
   - **Gate:** `troops ≥ triggerRatio · maxTroops`, else stop.
   - **Retaliate:** the non-friendly player with the largest incoming attack (a bot counts bot attackers; non-bots ignore them) → `sendAttack(it, force)`; stop if sent. `force` skips `shouldAttack`, which is always true for bot attackers anyway.
   - **Traitor:** with probability 1/3 attack a random neighbouring traitor; stop if sent.
   - Shuffle `nearby()`; for each non-friendly player: Human or Nation candidates are skipped with probability 1/2; `sendAttack`; stop at the first sent.

### 14.1 `sendAttack(target, force)` — shared by bots and nations

- Player target: if `sharesBorderWith(target)` → land attack; else **boat attack**: pick the closest pair (own shore border tile, target shore border tile); require `canBuildTransportShip` to that tile (§19.2); troops per §14.2 with base `troops/5`; launch a `TransportShipExecution`.
- Terra nullius: if any border tile has a 4-adjacent unowned passable land tile → land attack; else **boat to nearby TN**: for every 10th shore border tile, each cardinal direction whose first step is water, the tile 5 out must be unowned, passable, fallout-free land reachable by boat; send `min(troops/5, expansionCap)` if ≥ 1.

### 14.2 Troop sizing (`calculateAttackTroops`, bot-relevant form)

```
useReserve = target is a player and not (a Bot that owns structures)
             // "owns structures" = units().some(Structures.has): includes under
             // construction and marked-for-deletion (AiAttackBehavior.ts:1002)
ratio      = useReserve ? reserveRatio : expandRatio
land:  troops = attacker.troops − maxTroops · ratio
boat:  troops = attacker.troops / 5
cap    = ∞ for bot attackers
send nothing if troops < 1
```

Nation-only extensions (bot-attack sizing, send caps that keep a fraction of the strongest neighbour's army, too-weak filter, emoji) are in the Nation AI file.

---

## 15. Units and structures — common rules

### 15.1 Unit types

| Type | Kind | Health | Notes |
|---|---|---|---|
| City | structure, upgradable | — | §16 |
| DefensePost | structure | — | §17 |
| Port | structure, upgradable | — | §19.3 |
| Factory | structure, upgradable | — | §20 |
| MissileSilo | structure, upgradable | — | §21 |
| SAMLauncher | structure, upgradable | — | §21.3 |
| TransportShip | mobile | 1 | §19.2 |
| Warship | mobile | 1000 (+veterancy) | §19.4 |
| TradeShip | mobile | 1 | §19.3 |
| Train | mobile (engine, tail, 5 cars) | 1 | §20 |
| Shell, SAMMissile | projectile | 1 | §19.4, §21.3 |
| AtomBomb, HydrogenBomb, MIRV, MIRVWarhead | missile | 1 | §21 |

"Health 1" = no `maxHealth`; any damage kills. Structures have no health; they die by capture rules, nukes, level loss, deletion or owner death.

### 15.2 Counting (level-weighted)

- `unitCount(type)`: Σ level over owned units of `type`, **excluding** units under construction.
- `unitsOwned(type)`: under-construction units count 1, completed units count their level.
- `unitsConstructed(type)`: lifetime count of build *and upgrade* events (incremented at build start and on each upgrade).

### 15.3 Costs

`costWrapper(f, types…)`: `n = Σ_types min(unitsOwned(t), unitsConstructed(t))`, cost = `f(n + extra)`. Grouped types share one counter.

| Unit | Cost `f(n)` | Build ticks | Counter group |
|---|---|---|---|
| City | `min(1M, 2^n · 125k)` | 20 | City |
| Port | `min(1M, 2^n · 125k)` | 50 | Port + Factory |
| Factory | `min(1M, 2^n · 125k)` | 20 | Factory + Port |
| DefensePost | `min(250k, (n+1) · 50k)` | 50 | DefensePost |
| MissileSilo | `1M` | 100 | — |
| SAMLauncher | `min(3M, (n+1) · 1.5M)` | 300 | SAMLauncher |
| Warship | `min(1M, (n+1) · 250k)` | 0 | Warship |
| AtomBomb | `750k` | 0 | — |
| HydrogenBomb | `5M` | 0 | — |
| MIRV | `25M + 15M · (MIRVs launched game-wide)` | 0 | — |
| TransportShip, TradeShip, Train, Shell, SAMMissile, MIRVWarhead | 0 | 0 | — |

`2^n` is DetMath `pow2`. `instantBuild` config zeroes build times. Infinite-gold host cheats zero Human costs (not simulated).

### 15.4 Building (`ConstructionExecution`)

Preconditions (`canBuildUnitType`): unit not disabled; `gold ≥ cost`; player alive (except warheads); and for player-issued builds, not in the spawn phase.

- **Structures:** on the first tick, resolve a spawn tile (§15.5); `buildUnit` deducts the cost **immediately** and creates the unit; if the build time is > 0 the unit is `underConstruction` (ownership follows the unit if it is captured mid-build), then its type-specific execution starts.
  - **Exact timing.** A build issued on tick *t* adds a `ConstructionExecution`, initialised at the end of *t* (§3). Its first `tick`, on *t+1*, validates, deducts and creates the unit with `ticksUntilComplete = D` (`ConstructionExecution.ts:65-73`). Each later tick first checks `ticksUntilComplete === 0` → complete (`:91`), else decrements (`:97`). So the structure is created on *B = t+1* and completes on **B + D + 1**: City *t+22*, DefensePost *t+52*. The execution sits after the player executions in list order, so creation and completion both land after that tick's `PlayerExecution`s. Under-construction structures do not act, do not count in `unitCount`, and do not add city troop capacity, but they do block placement.
- **Non-structures** (warship, atom, hydrogen, MIRV): the construction execution completes instantly and hands off to the unit's own execution, which pays the cost when it creates the unit. Stacked nuke purchases (`amount > 1`) launch one `NukeExecution` each.

### 15.5 Placement

- **Land structures** (City, DefensePost, SAMLauncher, MissileSilo, Factory): the clicked tile must be owned by the builder. Candidates = the builder's own tiles reached by a 4-connected flood from the clicked tile, restricted to Euclidean distance² `< 15²` from it. A candidate is **blocked** if any structure of any owner, including under construction, is within Euclidean distance² `< 15²` (`structureMinDist = 15`). Choose the unblocked candidate closest to the clicked tile (stable on flood order N, S, W, E).
- **Port:** the builder's own **shore** tiles within Manhattan 20 of the clicked tile, sorted by Manhattan distance; the first that is also an unblocked land-structure candidate (above) for the clicked tile.
- **Warship:** the clicked tile must be water; spawns on the tile of the builder's nearest (Manhattan) active, completed port whose water component contains the clicked tile.
- **Nukes:** §21.2. **Transport ships:** §19.2. **Trade ships:** on an own port tile.

### 15.6 Upgrades

Upgradable: City, Port, Factory, MissileSilo, SAMLauncher. Finding the unit: the nearest own unit of that type within Euclidean 15 of the clicked tile (including under construction, but the chosen unit must be completed, not marked for deletion, and owned). Cost is the same formula as building the next one; paying raises `level` by 1 and increments `unitsConstructed`. Bulk upgrade: step *k* costs `f(n + k)`. SAMs and silos gain a missile slot that starts on cooldown (§21.1, §21.3).

`decreaseLevel` (used by damage systems): level −1; silos/SAMs lose a slot; at level 0 the unit is deleted.

### 15.7 Capture, deletion and death

- **Capture on territory change:** §6.1 step 2 (the *old* owner's execution does it). Structures change owner keeping their level; DefensePosts are destroyed instead.
- **Voluntary deletion (`DeleteUnitExecution`):** the unit must be the player's, active, on the player's own land, outside the spawn phase, and the per-player delete cooldown (300 ticks) must have elapsed. The unit is *marked* and deleted 300 ticks later (`deletionMarkDuration`); a capture in between clears the mark (`setOwner` → `clearPendingDeletion`, `UnitImpl.ts:231-232`).
  - **Exact timing.** All checks, and the mark, run in `init` at the end of the requesting tick *T* (`DeleteUnitExecution.ts:24-67`, `GameImpl.ts:501`). A failed check refuses without recording the cooldown. The mark sets `deletionAt = T + 300` (`UnitImpl.ts:313`). The execution's `tick` deletes when `ticks − deletionAt > 0` (`:322`), i.e. on **T + 301**, after that tick's player executions.
  - **Cooldown:** `canDeleteUnit` is `ticks − lastDeleteUnitTick ≥ 300` (`PlayerImpl.ts:1167`), with `lastDeleteUnitTick` starting at **−1** (`:171`). So the first delete is possible on tick 299, and the next 300 ticks after the last recorded one.
- **Owner death:** §6.1 step 3.
- **Nukes:** every non-missile unit within the blast (§21.4) is deleted.

---

## 16. Cities

Each completed City adds `level · 250_000` to its owner's `maxTroops` (before the Bot/Nation multiplier). Cities produce no gold directly. On completion, a City gets a train station if a Factory of any owner is within Euclidean 110 (§20).

---

## 17. Defense posts

### 17.1 Effect

A completed DefensePost owned by the defender and within Euclidean 30 of a tile being attacked multiplies that tile's `mag` by 5 and `tileCost` by 3 (§9). Not upgradable. Destroyed, not captured, when its tile changes hands.

### 17.2 Shooting — dead code at this anchor

`DefensePostExecution` contains target tracking and a `shoot()` that would fire shells every 100 ticks within range 75, but `tick()` never calls it. **Do not implement shooting.** Re-check on the next re-pin; if it is wired up, it reuses the warship shell (§19.4).

---

## 18. Water components and pathfinding

**Water components.** Upstream computes connected water components on a **2× downsampled minimap**, and resolves a full-resolution tile to a component by looking at its minimap cell, then its minimap 4-neighbours, then 2-hop neighbours (so shore land tiles resolve to the adjacent water body, and narrow straits survive downsampling). Components are rebuilt after water-nuke conversions (throttled to once per 20 ticks).

**C approach:** label 4-connected water components at full resolution once per map (and after any water conversion); a shore land tile's component is that of any 4-adjacent water tile (land tiles touching two bodies are rare; take the first in N, S, W, E order). Record the minimap approximation as a divergence.

**Water paths.** Upstream: hierarchical A* on the minimap, then line-of-sight smoothing that prefers deep water (magnitude ≥ 2–3), then local A* refinement near the endpoints. Units step one tile per tick along the resulting path. **C approach:** **4-connected A* over full-resolution water tiles with Manhattan heuristic**, path cached per (unit, destination) and recomputed when the destination changes. Travel times will differ slightly from upstream's smoothed paths; record it.

**Air paths** (shells, SAM missiles): a greedy staircase toward the target. From `(x, y)` to `(tx, ty)`: if aligned, step along the free axis; else with `ratio = floor(1 + |dy| / (|dx| + 1))`, step in x with probability `1/ratio`, otherwise in y.

**Missile trajectories** (nukes, MIRV): a cubic Bézier `P0 = src, P3 = dst`, `P1 = (x0 + dx/4, y0 + dy/4 − h)`, `P2 = (x0 + 3dx/4, y0 + 3dy/4 − h)` (upward arcs; `+h` for downward), `h = max(dist/3, 50)`, control-point y clamped to the map unless `ignoreMapBounds`. The missile advances `speed` units of arc length per tick; the tile is `floor` of the point. Flight time ≈ arc length / speed.

---

## 19. Naval

### 19.1 Shores and reachability

A player can reach a water body if one of its border tiles is a shore tile touching it. `closestReachableShore(targetOwner, attacker, tile, 50)`: the clicked tile if it is a shore tile of `targetOwner` on a water component the attacker's own shores touch; else the closest (Manhattan, first found on ties in DFS order N, S, W, E) such tile within Manhattan 50 of the click.

### 19.2 Transport ships (`TransportShipExecution`)

- **Limit:** at most 3 transport ships per player (`boatMaxNumber`).
- **Launch:** target = owner of the clicked tile at launch time (fixed). Reject if at the limit, if the target is the attacker, or if `!canAttackPlayer(target)`. If neither side is a Bot, the attacker's pending incoming alliance request from the target is rejected. Troops: requested amount (default `floor(troops/5)`), capped at the attacker's troops, **removed on launch** (floored). Destination `dst` = `closestReachableShore` (§19.1). Source = the attacker's shore border tile in `dst`'s water component closest to `dst` by water path.
- **Movement:** one tile per tick along the water path.
- **Arrival:** if `dst` is now owned by the attacker → survivors `troops × 0.75` return (25% malus). Otherwise the attacker **conquers `dst`** (so `dst` must still be passable land); then if the target has become friendly, all troops return; else a new `AttackExecution(troops, attacker, target, sourceTile = dst, removeTroops = false)` starts from the beachhead (§7). The boat is removed.
- **Retreat (`BoatRetreatExecution`):** sets `isRetreating`; the boat re-targets the attacker's shore tile closest by water path to its current position and sails there; arrival on own land applies the 25% malus as above. Also triggered automatically if `dst` becomes water (water nukes).
- **Failure:** no path, or no retreat destination → troops refunded in full, boat removed.
- **Death:** health 1 — one warship shell kills it and the troops are lost. Nukes reduce boat troops (§21.4) and delete boats inside the blast.
- A dead player's boats are deleted with its other units (§6.1).

### 19.3 Ports and trade ships

**Port tick** (completed ports only): ensure a train station if a Factory is within 110 (§20). Every 10 ticks (phase = port creation tick mod 10), roll once per level:

```
saturation(n) = (1 + 0.45·e^(−n/120)) · max(1 − sigmoid(n, ln2/50, 330), 0.25·(1 − sigmoid(n, ln2/100, 800)))
spawnRate     = max(1, floor(100 · (1/(rejections + 1)) / saturation(tradeShipsGlobal)))
spawn with probability 1/spawnRate; success resets rejections, failure increments (pity timer)
```

`tradeShipsGlobal` = level-weighted count of all trade ships on the map.

**Destination:** other players' ports whose owner can trade with this port's owner (no embargo in either direction, §22.4) and whose tile touches one of this port's water components; sorted by Manhattan distance. Weighted list: each port contributes `level` copies; plus `level` more if it is at Manhattan ≥ 300 **and** within the first `clamp(total/3, 4, total)` nearest; plus `level` more if at ≥ 300 **and** its owner is friendly. Pick uniformly from the weighted list.

**Trade ship** (`TradeShipExecution`): built at the source port for free, targeting the destination port; safe from pirates for 20 ticks after launch and for 20 ticks after every step onto a water tile with the shoreline bit (`safeFromPiratesCooldownMax = 20`). Moves one tile per tick along the water path, counting `tilesTraveled`.
- Deleted if the destination port's owner becomes the source port's owner; or (not captured) if the destination port dies or trade becomes impossible.
- **Captured** by a warship (§19.4): it reroutes to the captor's nearest completed, unmarked port in the same water component (none → deleted).
- **Arrival:** `gold = floor((75_000 / (1 + e^(−0.03·(d − 300))) + 50·d) · goldMultiplier)` with `d = tilesTraveled`. Uncaptured: **both** the source port's owner and the destination port's owner receive `gold`. Captured: the captor receives it (piracy). Recaptured by the original owner: the original owner receives it.

### 19.4 Warships and shells

**Warship** (`WarshipExecution`): built at a port (§15.5), `maxHealth = 1000 · (100 + 20·veterancy)/100`. State `patrolling | retreating | docked`, a patrol tile (the build click; changed by `MoveWarshipExecution` only within the same water component), a target tile and a target unit.

Per tick, in order:
1. Health ≤ 0 → delete.
2. **Healing:** +1 HP if within Euclidean 150 of any own port (none while the owner is under the Doomsday Clock, §23); if docked, an extra share of `port.level · 5` HP per tick split among ships docked at that port (fractional remainder carried per ship).
3. A manual patrol-tile change disables repair retreats for 50 ticks and cancels an ongoing one.
4. **Docked:** leave (return to patrolling) when fully healed or the port is gone; otherwise stay docked and do nothing else.
5. **Retreating:** shoot back at the best transport/warship target in range while sailing to the retreat port; switch to a clearly better port (closer by more than 25% in squared distance) or one with capacity; dock on reaching Euclidean ≤ 5 if the port has fewer docked ships than its level; if the port is full and the ship is fully healed, resume patrol.
6. **Start a repair retreat** if patrolling, health before this tick's healing `< floor(maxHealth · 75/100)`, and the owner has a port: go to the nearest own port in the same water component.
7. **Target selection** within Euclidean 130: exclude own units, units whose owner it cannot attack (friendly/immune, AFK counted friendly), units already sent a lethal shell, and docked warships. Trade ships are eligible only if the warship's owner has a reachable completed port, the trade ship is not safe from pirates, is not heading to a port of the warship's owner or of a friend, and is within Euclidean 100 of the warship's patrol tile. Priority: **TransportShip > Warship > TradeShip**, then nearest.
8. Transport or warship target → **shoot** and keep patrolling. Trade ship target → **hunt**: up to 2 steps per tick toward it (greedy water step when within Manhattan 20, path otherwise); capture when within Manhattan 5 (ownership transfers, §19.3). No target → **patrol**: sail to a random water tile (not shoreline, same water component) within ±50 of the patrol tile, widening by 50% after 500 failed draws, at most 3 times; then allow shoreline tiles.

**Shooting:** a shell fires if `ticks − lastShellAttack > 20`; firing at a warship resets `lastShellAttack`, firing at a transport ship does not (no reload against transports). If the target cannot survive a hit (health-1 units), it is marked so no second shell is sent.

**Shell** (`ShellExecution`): spawned at the shooter, moves up to 3 air-path steps per tick; on reaching the target's tile it deals `round(250/250 · m)` damage where `m = 200 + 25·(rng_int(1, 6) − 1)` (200–300), scaled by `(100 + 20·veterancy)/100` (floored) for veteran warships. Removed if the target dies, changes to the shell's owner, or 50 ticks after the shooter dies.

**Veterancy** (max 3): the killing blow on a warship grants a level immediately and resets progress; transports killed and trade ships captured share an integer progress meter where 10 transports or 25 captures fill a level (`10·25 = 250` points per level; transport = 25 points, capture = 10). Each level: +20% max health (no instant heal), +20% shell damage.

---

## 20. Rail network, factories and trains

**Stations.** A Factory always gets a station (and is the only station type that spawns trains); on completion it also gives a station to every City, Port and Factory within Euclidean 110 that lacks one. Cities and Ports get a station on completion if a Factory is within 110.

**Rails** (`RailNetworkImpl`): a new station first tries to **snap** onto an existing rail within 3 tiles, splitting it in two at the nearest rail tile. Otherwise it connects to up to 5 nearby City/Port/Factory stations within Euclidean 110 but farther than 15, nearest first, skipping any already reachable within 4 station hops, building a rail along a land path of length < 110·√2 ≈ 155.6.

**Rail paths** (`AStarRail`, run on the 2× minimap then upscaled): 4-connected A*; impassable tiles are never entered; water may be entered only from a shoreline tile or into a shoreline tile (so rails bridge narrow water at the coast but cannot cross open sea); step cost 1, or 6 if the destination is water or shoreline; +3 for every change of direction; heuristic weight 2. Rails ignore ownership — a rail may cross anyone's territory. Trains route over the station graph (`StationPathFinder`, not read: assume shortest path in station hops). Connected stations form **clusters**; clusters merge when rails join them and are recomputed when stations are removed.

**Train spawning** (factory stations): at most one train per 10 ticks per factory; requires the cluster to contain a City/Port station the factory's owner can trade with; per level, spawn with probability `1/trainSpawnRate`:

```
trainSaturation(n) = (1 + 0.5·e^(−n/30)) · max(1 − sigmoid(n, ln2/100, 560), 0.25·(1 − sigmoid(n, ln2/150, 900)))
trainSpawnRate     = max(1, floor((factoriesOwned + 10)·15 / trainSaturation(trainUnitsGlobal)))
```

`factoriesOwned` = the owner's level-weighted factory count; `trainUnitsGlobal` = all train units on the map (7 per train). Destination: a uniformly random eligible trade station (City or Port) in the cluster, other than the source.

**Train movement:** 2 rail tiles per tick along the station path. At each City/Port stop:

```
k    = max(0, stopsVisited − 9)           // first 10 stops unpenalised
gold = floor(max(5000, base − 5000·k) · goldMultiplier)
base = 10_000 self | 25_000 team | 25_000 other | 35_000 ally      (relation of train owner to station owner)
```

The train owner receives `gold`; if the station owner differs, it receives `gold` too. Factory stops pay nothing. The train is removed when it reaches its destination, when its next or current station is gone, or when the next station stops accepting trade with the train's owner.

---

## 21. Strategic weapons

### 21.1 Missile silos

`MissileSilo` holds a missile timer queue; slots = level. `launch()` pushes the current tick; the silo is **in cooldown when the queue length equals its level**; the front entry is removed once `ticks − front ≥ 90` (`SiloCooldown`). Upgrading pushes a timer too (the new slot starts cooling).

### 21.2 Launch rules (`nukeSpawn`)

No nukes while spawn immunity is active; target tile not impassable; target not owned by a teammate (unless the game is over, non-singleplayer); in team games, no Atom/Hydrogen whose outer radius would reach a teammate's structure. The launching silo is the owner's nearest (Manhattan) active, completed silo not in cooldown. MIRV additionally requires the target tile to be owned. Stacked launches from one silo are staggered by `waitTicks` so they depart on consecutive ticks.

### 21.3 SAM launchers and SAM missiles

- **Range:** `samRange(level) = 150 − 480/(level + 5)` → 70 at level 1, asymptotically 150. After an upgrade the range grows linearly to the new value over 45 ticks.
- **Cooldown:** timer queue like silos, `SAMCooldown = 90` ticks per slot, slots = level.
- **Targets:** Atom, Hydrogen and MIRV warheads (not the MIRV bus itself) within Euclidean 600, not already targeted by a SAM, not owned by the SAM's owner, not owned by a friendly player (except teammates after game over). Only considered while such missiles exist on the map.
- **Interception scheduling:** for each candidate, walk the missile's precomputed trajectory from its current index; the first tile that is flagged *targetable*, lies within the (time-projected) SAM range, and that the SAM missile can reach in time (`ceil(manhattan / 12)` ticks ≤ missile's ticks to that tile, including its remaining `waitTicks`) is the interception tile; fire when the scheduled launch tick is now or next tick. Fallback: the tile just before detonation, if the detonation tile is in range and targetable. Candidates never reachable are cached and skipped.
- **Priority when several are due:** score = `70_001 (Hydrogen) + max(0, 200_000 − 1000·manhattan(SAM, target tile)) + max(0, 10_000 − 100·ticksToImpact)`, highest first; fire while not in cooldown.
- **SAM missile:** moves 12 air-path steps per tick to the interception tile; on arrival the nuke is **destroyed with certainty** (no miss chance). Aborts if the target dies, becomes the SAM owner's, or the SAM dies.
- **Targetable window:** a nuke is targetable only within Euclidean 150 of its target tile or of its launch tile.

### 21.4 Atom and hydrogen bombs (`NukeExecution`)

| | inner | outer | speed (arc units/tick) |
|---|---|---|---|
| AtomBomb | 12 | 30 | 10 |
| HydrogenBomb | 80 | 100 | 10 |
| MIRVWarhead | 12 | 18 | 22 (+0..4 offset, §21.6) |

**On launch:** unit created at the silo (cost paid); the silo launches; the Bézier trajectory is precomputed with per-tile targetable flags; **alliance breaking** (not for warheads): every player whose weighted tile count in the blast exceeds 100 (inner tiles weight 1, ring tiles 0.5, over the full outer disk), or who owns any structure within the outer radius, has its alliance with the launcher broken (§22.2), its pending request from the launcher rejected (if one exists, that player is skipped instead of broken), and its relation toward the launcher set down by 100. The launcher's own pending requests from those players are rejected.

**Flight:** waits `waitTicks`, then advances along the curve. On arrival it detonates unless a SAM missile targeting it is within Euclidean 12 of the target tile.

**Blast tiles:** 4-connected BFS from the target tile accepting a tile iff `d² ≤ outer²` ∧ (`d² ≤ inner²` ∨ `chance(2)`) ∧ not impassable, with the RNG seeded by the detonation tick. **The random test is re-drawn on every visit attempt** (a rejected tile is not marked seen and can be accepted from another neighbour), so ring tiles are accepted with probability well above 1/2 and connectivity shapes the crater.

**Detonation effects:**
1. Every blast tile: if owned → relinquished; if land → fallout (or queued for water conversion in water-nuke mode). Water tiles are otherwise untouched.
2. Per affected player, with `T0 = tiles before the blast`, for each of its `k` destroyed tiles (`i = 0..k−1`, `left = T0 − i`): the player loses `deathFactor(troops, left)` troops (floored), each of its outgoing attacks loses `deathFactor(attackTroops, left)`, each of its transport ships loses `deathFactor(boatTroops, left)`:
   ```
   Atom/Hydrogen: deathFactor(h, left) = 5·h / max(1, left)
   MIRVWarhead:   deathFactor(h, left) = 500·(1 − e^(−2·max(0, h − 0.03·maxTroops)/maxTroops))
   ```
3. **Every unit** (any owner, including the launcher's) within Euclidean distance² `< outer²` of the target, except missiles and SAM missiles, is deleted.

### 21.5 Fallout and water nukes

Fallout (state bit 13) marks unowned land; conquering a tile clears it. Effects: excluded from the win denominator (§12); attack modifier `5 − 2·falloutRatio` on `mag` and `tileCost` (§9); ignored by `nearby()` and by AI terra-nullius detection. Nothing else decays fallout.

**Water-nuke mode** (config, off by default): the blast outline is a smoothed random radius between inner and outer at 16 angular samples, and destroyed land becomes lake water: `numLandTiles` drops, the ocean bit propagates from adjacent ocean, water magnitudes and shorelines are recomputed, and water components are rebuilt. Boats whose destination becomes water retreat. Defer; it is the only rule that mutates terrain.

### 21.6 MIRV (`MirvExecution`)

Launched from a silo at an owned target tile. **Always** breaks the launcher's alliance with the target's owner and sets both players' relations toward each other down by 100. The bus flies a Bézier to a separation point (`x` midway between silo and target, `y = max(0, targetY − 500) + 50`) at a speed normalised so the flight takes about 14 ticks (square-root compression for longer flights). While 20..11 ticks from separation it stages up to **350** warhead targets: random tiles within Euclidean 1500 of the target that are land owned by the target player and at Manhattan ≥ 55 from every staged target. At ≤ 10 ticks it finalises (drops targets no longer owned by the target player, tops up), sorts farthest-first, and schedules warheads from the separation point at speed 22 + {0,1,2,3,4} for indices {<70, <140, <210, <280, rest}, each with `waitTicks = remaining + rng_int(0, 15)`. Warheads use the MIRV death factor, do not break alliances, and can be intercepted by SAMs. If the bus is destroyed first, all scheduled warheads are cancelled. Every constant here assumes maps of 10^5–10^6 tiles (§24).

---

## 22. Diplomacy

### 22.1 Relations

Each player holds a relation value toward each other player, clamped to [−100, 100], default 0. Bands: `< −50` Hostile, `< 0` Distrustful, `< 50` Neutral, else Friendly. Every tick (§6.1 step 1) each value moves 0.05 toward 0 and snaps to 0 once `|r| < 0.1`.

| Event | Change (to whose view) |
|---|---|
| Attack launched at a player | target → attacker: −60 / −70 / −80 / −100 by difficulty |
| Targeted (`TargetPlayerExecution`) | target → targeter: −40 |
| Alliance broken | betrayed → breaker: −100; every `nearby()` player of the breaker not on the betrayed's team → breaker: −40 |
| Nuke blast (alliance-break set) | affected → launcher: −100 |
| MIRV | target ↔ launcher: −100 both ways |
| Mutual alliance request accepted | both: +100 |
| Troop donation ≥ threshold | recipient → sender: +50. Threshold = `rng_int(maxT/13, maxT/11)` Easy, `/11–/9` Medium, `/9–/7` Hard, `/7–/5` Impossible, where `maxT` is the recipient's max troops |
| Gold donation | recipient → sender: `min(100, 5 · floor(gold / chunk))`, `chunk = round(c + c·ticks/(3000 + spawnPhaseTurns))`, `c` = 2,500 / 5,000 / 12,500 / 25,000 by difficulty (the chunk grows with game time) |
| Embargo against a Nation | Nation → embargoer: −20 while it lasts (Nation AI, reverted when lifted) |

Relations drive only AI decisions; no mechanic reads them otherwise.

### 22.2 Alliances

- **Request** (`AllianceRequestExecution`): allowed if alliances are enabled, the other player is not self, neither is disconnected, they are not already friendly, the requester is alive, no request to them is pending, and 300 ticks have passed since the requester's last request to them (unless the other side has a pending request to the requester, which is always allowed). A request expires (rejected) after 200 ticks.
- **Mutual request:** if the recipient already has a pending request to the requester, it is accepted at once: both relations +100, temporary embargoes between them lifted, and in-flight missiles between them cancelled: each side's active MIRV aimed at the other's territory, MIRV warheads whose target tile the other owns, and atom/hydrogen bombs whose blast would break the alliance by the §21.4 test (weighted tiles > 100 or a structure in radius) are deleted.
- **Duration:** 3000 ticks (5 min; host-configurable 1–15 min; 0 disables alliances). In the last 300 ticks both sides may agree to extend; when both have agreed, `expiresAt = now + duration`. Expiry is checked in each member's `PlayerExecution`.
- **Effects:** allies are friendly (§6.5): they cannot attack each other; attacks already in flight retreat without malus; they can donate; trade and train routes favour allies; nukes are restricted (§21.2); warships and SAMs ignore allied units.
- **Breaking** (`BreakAllianceExecution`): marks the breaker **traitor for 300 ticks** unless the betrayed is itself a traitor or disconnected; relation effects per §22.1. Attacks against a traitor pay loss ×0.5 and cost ×0.8 (§9); bots and nations preferentially attack neighbouring traitors.
- Alliances also end on member death (silently) and by nuke/MIRV alliance breaking.

### 22.3 Targeting

`TargetPlayerExecution`: allowed if not self, not friendly, and no target was set in the last 150 ticks; a target lasts 100 ticks. Allies' targets are visible (`transitiveTargets`) and drive the Nation AI's "assist allies" strategy.

### 22.4 Embargoes

A player may embargo another; `canTrade(a, b)` requires no embargo in either direction. Kinds: **permanent** (explicit, via `EmbargoExecution`; `EmbargoAllExecution` applies to every non-bot, non-teammate with a 100-tick cooldown) and **temporary** (automatic: the target of an attack between non-bots embargoes the attacker; lifted after 3000 ticks, or when the two ally). A temporary embargo never overwrites a permanent one. Embargoes block trade-ship destinations and train stops, and nothing else.

### 22.5 Donations

Troops or gold to a friendly, alive player; per-recipient cooldown 100 ticks; config flags can forbid donations *to Humans*. Default amounts: `floor(troops/3)` troops, capped at the recipient's `maxTroops − troops`; `gold/3`. Donated amounts are removed from the sender (floored) and added to the recipient.

---

## 23. Game modes and optional systems

- **Teams:** players are assigned to colored teams (or Humans vs Nations; Duos/Trios/Quads). Teammates are friendly. Team wins use summed tiles (§12); Bots form their own team that can never win. Team spawn areas constrain random spawns.
- **Difficulty** (Nations): start troops, max troops, growth (§6.3), attack relation penalty (§7.1), Nation AI cadence and strategy order. Bots are unaffected.
- **Doomsday Clock** (anti-stall, off by default): a rising required map share in waves; a side below it gets a 30 s warning, then troops drain toward a floor of 5% of max, the floor itself decaying from 40% over 90 s, and territory rots away over 150 s. Doomed sides cannot heal warships. Constants in `Config.ts` `DOOMSDAY_CLOCK_DEFAULTS`; the execution was not read. **Defer.**
- **Overtime** (off by default): win threshold falls 2 percentage points per minute after minute 30 (§12).
- **Ranked 1v1/2v2**, **host cheats** (infinite gold/troops, gold multiplier), **disabled units**, **instant build**, **random spawn**, **water nukes**: config switches; implement as flags only when needed.

---

## 24. Scale, and what is not simulated

### 24.1 Scale-bound constants

Every one of these is tuned for maps of 10^5–10^6 land tiles (the smallest shipped OpenFront map is ~350×350). At 48×48 most of them exceed the map:

| Constant | Value | Where |
|---|---|---|
| Wipe / annex-always thresholds | 100 tiles | §10, §6.1 |
| maxTroops floor | 50,000 | §6.3 |
| Large-territory midpoint | 300,000 tiles | §9 |
| Spawn disk / min distance | radius 4 (52 tiles) / 30 | §13 |
| Structure min distance | 15 | §15.5 |
| Port search | Manhattan 20 | §15.5 |
| Defense post range | 30 | §17 |
| Transport landing search | Manhattan 50 | §19.1 |
| Trade short-range debuff | 300 | §19.3 |
| Warship patrol / targeting / heal range | 100 / 130 / 150 | §19.4 |
| Train station range | 15–110 | §20 |
| SAM range / detection | 70–150 / 600 | §21.3 |
| Nuke radii | 12/30, 80/100, 12/18 | §21.4 |
| Nuke targetable window | 150 | §21.3 |
| MIRV range / spread / count | 1500 / 55 / 350 | §21.6 |
| Bézier minimum height | 50 | §18 |

The previous edition's §14 rescale policy stands for the territorial core — implement as written, then rescale knowingly, and record every applied rescale in the project reference's table. **Coupled pairs move together or not at all:** the maxTroops floor with start troops and the City troop bonus; the wipe threshold with the annex-always threshold. For Tiers B–D the practical conclusion is that the full game needs real-scale maps (≥ ~125k tiles); shrinking every constant proportionally is a second game, not a port. **PR #1 exception:** Tier B-lite (§0) ships at 48×48, so the defense-post range, structure min-distance and the cost/income-vs-episode-length ratio are rescaled knowingly — each was an open decision in §0 and now has a row in the project reference's rescale table (all answered 25 Sept).

### 24.2 Not simulated

Emoji and quick chat, display messages, stats (`StatsImpl`), motion-plan recording, packed client updates and hashes, pause, disconnect handling and AFK snapshots, cosmetics and names, the nukeable-layer impact queue (client rendering), ranked cancellation, spectators.

---

## 25. Conformance delta: C sim vs this spec

**Tier A complete, 25 Sept 2026 (`4a3d2848`).** Commits, verification and measured effects are in the project reference (§1) and `docs/history.md`. This section keeps only what bears on future work.

| # | Item | Status |
|---|---|---|
| 1 | Combat math (§9) | done: 1a border set `908b785a`, 1b formulas `c4faca4a` |
| 2 | Integer player troops (§2.1) | done: 2a double `39db150f`, 2b `int64_t` `84ebc825` |
| 3 | DetMath + no FP contraction (§2.2) | done, Mac↔x86 identical |
| 4 | Annexation (§11) | done: 4a capturer `9b542489`, 4b hole selection `d6aef90c`; fast path is a no-op for us |
| 5 | Attack init (§7.1) | floored deduction done via 2b; land-only combination inert until boats |
| 6 | Manual retreat (§7.4) | not implemented; needed when retreat becomes an action |
| 7 | Spawn (§13) | disk done `5175a70e`; phase equivalent by construction; immunity → Phase 3 |
| 8 | Bot driver (§14) | river `nearby()`/boats → Tier C; fallout → Tier D; alliances/traitor → Tier E; scrapping live with B-lite |
| 9 | Dead-defender wipe (§10) | conformant; target-friendliness inert without alliances; `conquerPlayer` gold live with B-lite |
| 10 | Win check (§12) | fallout denominator inert until Tier D |
| 11 | Per-attack RNG (§2.3) | divergence kept: ours draws from `e->rng` |
| 12 | `relinquish` (§5.2) | not implemented; needed by nukes |

Plus a fix outside the list: attack troops clamp at 0 on every write (`eea12848`, §7.3).

**Carry forward:**
- **B-lite gold transfer has two call sites:** the dead-defender wipe (item 9) and `removeCluster` when the collected set is the player's whole territory (item 4).
- **`on_map_edge` must count impassable-adjacent tiles** (§4) in the same commit that introduces impassable terrain.
- **Recorded divergences:** per-attack RNG (item 11); iteration order — upstream walks JS `Set`s in insertion order in annexation cluster formation and the dead-defender pass, ours walks `TileSet` order.
- Applied 48×48 rescales live in the project reference's rescale table.

---

## 26. Nation AI

**Deferred.** Specified separately in `openfront_nation_ai_spec.md`, which is partial: cadence, attack flow, the full strategy table, troop sizing and the alliance-decision logic are read; the structure, warship, nuke and MIRV behaviours and `NationCreation` are not. Tier G does not block Tiers A–F: bots (§14) are the scripted opponents until then, and the Phase 3 self-play plan does not need Nations at all.

---

## 27. Code

The C API is the header, `ocean/openfront/openfront.h`; read it for signatures. Implementation notes, settled decisions and working conventions are in the project reference (§3, §4, §7). No copy of the API lives in this file.
