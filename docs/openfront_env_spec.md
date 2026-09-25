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

Tier A comes first because the existing C sim implements the `fc50009` core, and several of its mechanics are now wrong (§1, §25). Re-deriving the core is behaviour-changing work: each change is its own commit with its own new `hist_run` baseline. Progress is tracked in §25.

**PR #1 stopping point (decided 24 Sept 2026): Tier A + "Tier B-lite".** The full-game scope stands as the long-run plan, but PR #1 stops at the smallest build that contains the game's actual decision loop: spend troops on land, spend gold on troop capacity (City), or spend gold to hold a border cheaply (Defense Post). Without gold and structures the agent has one decision (which neighbour to hit), which is why the current build plays like territorial.io rather than OpenFront.

Tier B-lite is exactly:

| In PR #1 | Sections |
|---|---|
| Gold income; `conquerPlayer` gold transfer | §6.4, §5.3 |
| Costs, construction, placement, capture — City and DefensePost only | §15.2–§15.5, §15.7 |
| City troop capacity | §16 |
| Defense-post attack modifier (posts do not shoot at this anchor) | §17.1 |
| Bot structure scrapping (bots mark captured structures for deletion) | §14 step 2, §15.7 |

Out of PR #1: upgrades (§15.6, open — see below), every other unit type, and Tiers C–H. Later PRs, in likely order: nukes (Tier D — the spectacle, second PR), then naval (C) and rail/Factories (F) together with real-scale maps, where water pathing and 110-tile rail geometry mean something. At 48×48 every City would sit within station range of every Factory and the rail network would be degenerate. Diplomacy (E) only makes sense with self-play.

**Open decisions for Tier B-lite (record the answers in the project reference):**

1. **Economy vs episode length.** A human earns 100 gold/tick; the first City costs 125k = 1,250 ticks against a ~1,900-tick episode. At upstream prices structures barely occur. Scale costs or income, or lengthen episodes — a documented rescale either way (§24.1).
2. **Defense-post radius.** 30 tiles covers most of a 48×48 map. Rescale with the map, like the wipe threshold.
3. **Structure min-distance** (15, §15.5) — same question, smaller stakes.
4. **Action space.** Discrete-7 → Discrete-9 (`build_city`, `build_post`) with automatic placement: City on the deepest interior tile, post on the border facing the most dangerous neighbour. Extends the settled Discrete-7 decision; does not reopen it.
5. **Upgrades.** Probably out (a second City is the same decision as a City upgrade); confirm.
6. **Tier A trim — decided 25 Sept 2026.** Spawn disk landed; spawn phase is equivalent by construction; spawn immunity moves to Phase 3; river-crossing `nearby()` moves to Tier C (it needs boats). The combat rewrite (item 1) may not: defense posts plug directly into the §9 formulas.

---

## 1. Corrections to the `fc50009` edition — read first

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

All other arithmetic is IEEE double. **The C sim uses `float`; full fidelity requires `double` for the sim math** (troop growth, combat, economy curves). Record the choice either way.

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
- **`isOnEdgeOfMap(t)`: on the map boundary, or any 4-neighbour is impassable.** (Changed since `fc50009`; §1 item 3.)
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
4. `startTroops` defaults to `attackAmount` = `troops/20` if the attacker is a Bot, else `troops/5`. Every AI caller passes an explicit amount; the agent's `apply_action` sends `troops/5` (project decision, see project reference §5).
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
2. If the delete cooldown allows (§15.7), mark the first structure it owns for deletion — bots scrap structures they capture.
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

- **Structures:** on the first tick, resolve a spawn tile (§15.5); `buildUnit` deducts the cost **immediately** and creates the unit; if the build time is > 0 the unit is `underConstruction` for that many ticks (ownership follows the unit if it is captured mid-build), then its type-specific execution starts. Under-construction structures do not act, do not count in `unitCount`, and do not add city troop capacity, but they do block placement.
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
- **Voluntary deletion (`DeleteUnitExecution`):** the unit must be the player's, active, on the player's own land, outside the spawn phase, and the per-player delete cooldown (300 ticks) must have elapsed. The unit is *marked* and deleted 300 ticks later (`deletionMarkDuration`); a capture in between clears the mark.
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

The previous edition's §14 rescale policy stands for the territorial core — implement as written, then rescale knowingly, and record every applied rescale in the project reference's table. **Coupled pairs move together or not at all:** the maxTroops floor with start troops; the wipe threshold with the annex-always threshold. For Tiers B–D the practical conclusion is that the full game needs real-scale maps (≥ ~125k tiles); shrinking every constant proportionally is a second game, not a port. **PR #1 exception:** Tier B-lite (§0) ships at 48×48, so the defense-post range, structure min-distance and the cost/income-vs-episode-length ratio are rescaled knowingly — each is an open decision in §0 and gets a row in the project reference's rescale table once made.

### 24.2 Not simulated

Emoji and quick chat, display messages, stats (`StatsImpl`), motion-plan recording, packed client updates and hashes, pause, disconnect handling and AFK snapshots, cosmetics and names, the nukeable-layer impact queue (client rendering), ranked cancellation, spectators.

---

## 25. Conformance delta: current C sim vs this spec

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

## 26. Nation AI

**Deferred.** Specified separately in `openfront_nation_ai_spec.md`, which is partial: cadence, attack flow, the full strategy table, troop sizing and the alliance-decision logic are read; the structure, warship, nuke and MIRV behaviours and `NationCreation` are not. Tier G does not block Tiers A–F: bots (§14) are the scripted opponents until then, and the Phase 3 self-play plan does not need Nations at all.

---

_§27 and §28 are carried verbatim from the `fc50009` edition (its §15 and §16). They describe the C code as it stands, which conforms to that edition, not this one; §25 lists the gap._

## 27. Code state & API contract (openfront.h)

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


## 28. Working conventions (cross-chat)

- **Claude writes the game simulation code; Sam audits it. Puffer binding setup is done together.** (Standing decision, Aug 2026 — this replaces the earlier convention where Sam wrote all core sim code.) The audit is the load-bearing half: Joseph reviews PRs on stream and asks implementation questions, so anything Sam cannot explain unprompted is not done. Annexation in particular — be able to explain why the cluster fill runs over the border set rather than the territory, and why the enemy bbox contains the cluster bbox rather than the reverse, without looking.
- Claude verifies any handed-over code compiles and passes in the container before handing it over.
- **Hand over whole functions, never excerpts.** Excerpt boundaries are exactly where adjacent required lines get dropped; this produced three clean-compiling defects in one pass (project reference §7). The same rule applies to these markdown files: replace whole sections, not lines.
- Recurring bug classes to watch: `=` vs `==`, `.` vs `->`, missing braces, missing return, struct-by-value vs pointer, Python-isms, nested function defs, **tile-number vs player-id confusion** (everything is a bare int: tiles are `t`/`nb[k]`/heap contents; players are `p`/`attacker`/`target`/`owner[...]`), and **`heap_push` arg transposition** (int/float convert silently both ways — the compiler will not catch it).
- Placeholder economics before real economics. One variable per experiment. Don't run phases ahead of where the code is.
- **A refactor is not verified until a one-variable control is bit-identical.** When a structural change lands alongside behavioural ones, revert the behavioural ones and diff against the pre-change build before layering them back one at a time. "Direction and rough magnitude match" is not verification; it is where a transposed argument hides. Note that a *stale global reference* is not the risk after a globals→struct move — the globals are gone, so it won't compile. The risk is a transposed argument or wrong index, which compiles clean and passes every invariant.
- **Don't reason about cache from `sizeof`.** A 5.5× reduction in `sizeof(Env)` produced 0.12% throughput change, because untouched `calloc` pages are never faulted in and the actual working set was ~24 KB either way. Measure the touched footprint, not the allocation.
- **Code that is never executed is not verified.** The standalone harness calls no `puf_*` function, so the whole binding compiled and proved nothing until `drive_test.c` existed to drive it — and that harness found two clean-compiling defects on its first run. Whenever a new interface is added, add the thing that exercises it in the same pass.
- Sam doesn't read TypeScript and doesn't need to — this spec is the interface to the source. Terse prose, no filler, settled things stay settled.
