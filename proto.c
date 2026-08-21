/* OpenFront-inspired N-player territory conquest sim.
   Mechanics reimplemented from openfrontio/OpenFrontIO (AGPL-3.0) per
   openfront_env_spec.md, source anchor commit fc50009af1fb, 18 Aug 2026.
   Rules and constants are not copyrightable expression; this is not a
   transliteration. Credit: OpenFront.io.

   Phase 0/1 standalone prototype. Every piece of simulation state lives in
   struct Env; nothing is file-static except pure compile-time-shaped helpers
   and the test scaffolding. That is the precondition for PufferLib, which
   steps thousands of envs under `#pragma omp parallel for` (src/pufferl.cu),
   so a single shared array or generation counter would corrupt silently and
   nondeterministically.

   build:
     gcc -g -O0 -Wall -Wextra -DDEBUG -fsanitize=address,undefined proto.c -o proto_dbg -lm
     gcc -O2 -Wall -Wextra proto.c -o proto_fast -lm
   -lm is required (powf). -DDEBUG is load-bearing: without it run_tests() and
   check_borders() compile to nothing and the program passes silently. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define W 48
#define H 48
#define N (W*H)
#define MAXP 9            /* players 1..8; 0 = terra nullius */
#define MAXATK 32

/* Was 8*N (18432). Measured peak across 300 episodes was 278 entries, and the
   structural bound is 4N (a tile is enqueued once per attacker-owned
   4-neighbor) but only if a single attack owns the entire map frontier. 8*N
   put sizeof(Env) at 5.1 MB, which is not viable when PufferLib allocates one
   Env per agent slot up front. 2048 keeps ~4.7x headroom over the peak
   measured across 3000 scripted-bot episodes (440); heap_full_drops counts any
   push refused at cap so a cap hit is loud instead of a silently truncated
   frontier. The scripted-bot peak is a floor, not a bound: a trained policy
   that sprawls thin holds longer frontiers, so re-read heap_peak in Phase 2
   before treating this as settled. */
#define HEAPCAP 2048

#define ANNEX_PERIOD 20              /* source ticksPerClusterCalc */
#define SPAWN_TILES  49              /* radius-4 Euclidean disk, matches source's ~49 */
#define WIPE_TILES   (SPAWN_TILES/3) /* rescaled dead-defender threshold, spec 14 */
#define ANNEX_TILES  WIPE_TILES      /* rescaled always-check threshold, spec 9 */

#define SPAWN_RADIUS   4
#define SPAWN_MIN_DIST 13    /* Manhattan between centers, scaled to 48x48 */
#define SPAWN_TRIES    1000
#define SPAWN_RELAX    750   /* min-distance dropped after this many attempts */

/* Source values. These are coupled to the +50000 floor in max_troops (spec 14
   item 2): start troops were previously 1000 with the floor left as written,
   which is the one indefensible combination — the bot expand/reserve gates key
   off max_troops, the floor dominates it at 48x48, and bots spawned unable to
   afford their first attack (mean tick of first attack 106 vs 7). Restored to
   source. If the floor is ever rescaled, these two move with it in the same
   commit. */
#define START_TROOPS_HUMAN 25000.0f
#define START_TROOPS_BOT   10000.0f

/* ---------------- types ---------------- */

typedef struct {
    int tiles[N];
    int pos[N];
    int count;
} TileSet;

typedef struct {
    float pri[HEAPCAP];
    int   tile[HEAPCAP];
    int   count;
} Heap;

typedef struct {
    TileSet tiles;
    TileSet border;
    float troops;
    int alive;
} Player;

typedef struct {
    int   active;
    int   attacker;
    int   target;
    float troops;
    Heap  heap;
} Attack;

typedef struct {
    int attack_rate;      /* ticks between decisions, 40..79 */
    int attack_off;       /* phase offset, 0..attack_rate-1 */
    float trigger_ratio;  /* 0.50..0.59 */
    float reserve_ratio;  /* 0.30..0.39 */
    float expand_ratio;   /* 0.10..0.19 */
    int neighbors_tn;     /* starts 1, cleared permanently when no TN left */
} Bot;

typedef struct Env Env;
struct Env {
    /* map */
    unsigned char  terrain[N];   /* 0 water, 1 plains, 2 highland, 3 mountain */
    unsigned short owner[N];     /* 0 = terra nullius, else player id */
    int  land_tiles;
    long ticks;

    /* rng. Per-env, never global: PufferLib runs thousands of envs in parallel
       and a shared stream correlates their episodes. */
    unsigned int rng_state;

    Player players[MAXP];

    /* Player type. Source has Human / Nation / Bot; we hardcode two (spec 13).
       Every seat is a Bot until the agent seat flips one to 0. Four handicaps
       read it: max_troops/3, growth x0.5, terra-nullius attacker loss mag/10
       vs mag/5, and the human-attacking-bot mag *= 0.7 modifier. */
    unsigned char is_bot[MAXP];

    /* Annexation scheduling (spec 9). last_calc carries a per-seat offset so
       the cluster scan cost spreads across ticks instead of spiking on one;
       last_tile_change is the cheap-out that skips players whose territory has
       not moved since their last scan. */
    long last_calc[MAXP];
    long last_tile_change[MAXP];

    Attack attacks[MAXATK];
    Bot    bots[MAXP];
    int    spawn_center[MAXP];

    /* Annexation scratch. Two independent generation counters, deliberately:
       annex_remove runs INSIDE annex_tick's component loop, so sharing one
       counter between the border-component enumeration and the territory fill
       corrupts the enumeration mid-iteration. Both DFS stacks are bounded by N
       only because tiles are marked visited before push. */
    unsigned int cl_visited[N];
    unsigned int cl_gen;
    int cl_stack[N];
    int cl_comp[N];
    int cl_start[N];
    int cl_size[N];

    unsigned int ff_visited[N];
    unsigned int ff_gen;
    int ff_stack[N];
    int ff_take[N];

    /* per-env counters (were file-static; under OMP those would race) */
    long annex_events;
    long annex_tiles_moved;
    long spawn_failures;
    long heap_full_drops;
    int  heap_peak;
};

/* ---------------- pure helpers (no env state) ---------------- */

static int ref(int x, int y) { return y*W + x; }
static int rx(int r) { return r % W; }
static int ry(int r) { return r / W; }

static float within(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* N, S, W, E — the order source uses everywhere. Returns count. */
static int neighbors(int r, int *out) {
    int n = 0;
    if (ry(r) != 0)   out[n++] = r-W;
    if (ry(r) != H-1) out[n++] = r+W;
    if (rx(r) != 0)   out[n++] = r-1;
    if (rx(r) != W-1) out[n++] = r+1;
    return n;
}

/* 8-connectivity, annexation cluster fill only (source
   forEachNeighborWithDiag). Offset arithmetic rather than the obvious dx/dy
   double loop calling ref(): that loop measured 9% of total throughput. */
static int neighbors8(int r, int *out) {
    int x = r % W, y = r / W, n = 0;
    int up = (y != 0), dn = (y != H-1), lf = (x != 0), rt = (x != W-1);
    if (up) { if (lf) out[n++] = r-W-1; out[n++] = r-W; if (rt) out[n++] = r-W+1; }
    if (lf) out[n++] = r-1;
    if (rt) out[n++] = r+1;
    if (dn) { if (lf) out[n++] = r+W-1; out[n++] = r+W; if (rt) out[n++] = r+W+1; }
    return n;
}

static int on_map_edge(int t) {
    int x = rx(t), y = ry(t);
    return x == 0 || x == W-1 || y == 0 || y == H-1;
}

/* ---------------- rng ---------------- */

/* xorshift32 (13, 17, 5): one full cycle of 2^32-1 over the nonzero states.
   Chosen for cost, not quality — three shifts and three xors, no multiply, no
   memory traffic. This is jitter for map blobs and bot personalities, not
   Monte Carlo. */
static void rng_seed(Env *e, unsigned int s) {
    /* PufferLib assigns env->rng = env index before puf_init, so seat 0 would
       get 0 — the xorshift fixed point, and 1,2,3... are adjacent states whose
       early output is correlated. Scramble first (splitmix32 finalizer), then
       coerce zero. */
    s ^= s >> 16; s *= 0x7feb352du;
    s ^= s >> 15; s *= 0x846ca68bu;
    s ^= s >> 16;
    e->rng_state = (s == 0) ? 1u : s;
}

static unsigned int rng_next(Env *e) {
    unsigned int x = e->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng_state = x;
    return x;
}

/* uniform in [0, n), n > 0. Modulo bias is ~n/2^32, about 1e-8 for n <= 48. */
static int rng_below(Env *e, int n) {
    return (int)(rng_next(e) % (unsigned int)n);
}

/* Matches source PseudoRandom.nextInt(min, max): min INCLUSIVE, max EXCLUSIVE
   (PseudoRandom.ts computes floor(next()*(hi-lo))+lo). Keeping the exclusive
   convention here means every nextInt(a, b) in the TS transcribes to
   rng_int(a, b) argument-for-argument — never adjust arguments while
   transcribing. Call sites below are verbatim from source. */
static int rng_int(Env *e, int lo, int hi) {
    if (hi <= lo) return lo;   /* nextInt(0,0) is 0 in JS; avoid mod-by-zero */
    return lo + rng_below(e, hi - lo);
}

/* ---------------- map ---------------- */

/* Source distinguishes isOceanShore (ocean bit) from isShore (shoreline bit).
   We have neither — one water value, no lakes — so both collapse to "land tile
   touching water". Spec 13 files this: once real map gen creates lakes the two
   predicates separate and annexation behaviour changes. */
static int is_shore(Env *e, int t) {
    if (e->terrain[t] == 0) return 0;
    int nb[4];
    int n = neighbors(t, nb);
    for (int i = 0; i < n; i++)
        if (e->terrain[nb[i]] == 0) return 1;
    return 0;
}

static void fill_terrain(Env *e) {
    e->land_tiles = 0;
    for (int r = 0; r < N; r++) {
        if (ry(r) == 0 || ry(r) == H-1 || rx(r) == 0 || rx(r) == W-1)
            e->terrain[r] = 0;
        else
            e->terrain[r] = 1;
    }

    for (int i = 0; i < 15; i++) {
        int cx  = rng_below(e, W);
        int cy  = rng_below(e, H);
        int rad = 4 + rng_below(e, 8);
        int inner = rad * 2 / 3;

        for (int y = cy-rad; y <= cy+rad; y++) {
            for (int x = cx-rad; x <= cx+rad; x++) {
                if (x < 0 || x >= W || y < 0 || y >= H) continue;
                int r = ref(x, y);
                if (e->terrain[r] == 0) continue;
                int dx = x - cx;
                int dy = y - cy;
                int d2 = dx*dx + dy*dy;
                if (d2 <= rad*rad)
                    e->terrain[r] = (d2 <= inner*inner) ? 3 : 2;
            }
        }
    }
    for (int r = 0; r < N; r++)
        if (e->terrain[r] != 0) e->land_tiles++;
}

static void print_owner(Env *e) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r = ref(x, y);
            if (e->terrain[r] == 0)    putchar('~');
            else if (e->owner[r] == 0) putchar('.');
            else                       putchar('0' + e->owner[r]);
        }
        putchar('\n');
    }
}

/* ---------------- tile set ---------------- */

static void ts_init(TileSet *s) {
    s->count = 0;
    for (int i = 0; i < N; i++) s->pos[i] = -1;
}

static void ts_add(TileSet *s, int t) {
    if (s->pos[t] != -1) return;
    s->tiles[s->count] = t;
    s->pos[t] = s->count;
    s->count++;
}

static void ts_remove(TileSet *s, int t) {
    if (s->pos[t] == -1) return;
    int i = s->pos[t];
    int last = s->tiles[s->count - 1];
    s->tiles[i] = last;
    s->pos[last] = i;
    s->pos[t] = -1;
    s->count--;
}

static int ts_has(TileSet *s, int t) { return s->pos[t] != -1; }

/* ---------------- heap ---------------- */

static void heap_init(Heap *h) { h->count = 0; }

/* TILE FIRST, PRIORITY SECOND. int and float convert silently in both
   directions, so a transposition compiles clean and means nothing. */
static void heap_push(Env *e, Heap *h, int t, float p) {
    if (h->count >= HEAPCAP) { e->heap_full_drops++; return; }
    int i = h->count++;
    h->tile[i] = t;
    h->pri[i]  = p;
    if (h->count > e->heap_peak) e->heap_peak = h->count;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->pri[parent] <= h->pri[i]) break;
        float fp = h->pri[parent];
        int   ft = h->tile[parent];
        h->pri[parent]  = h->pri[i];
        h->tile[parent] = h->tile[i];
        h->pri[i]  = fp;
        h->tile[i] = ft;
        i = parent;
    }
}

static int heap_pop(Heap *h) {
    if (h->count == 0) return -1;
    int best = h->tile[0];

    h->count--;
    if (h->count == 0) return best;

    h->tile[0] = h->tile[h->count];
    h->pri[0]  = h->pri[h->count];

    int i = 0;
    for (;;) {
        int l = 2*i + 1, r = 2*i + 2, small = i;
        if (l < h->count && h->pri[l] < h->pri[small]) small = l;
        if (r < h->count && h->pri[r] < h->pri[small]) small = r;
        if (small == i) break;

        float fp = h->pri[small];
        int   ft = h->tile[small];
        h->pri[small]  = h->pri[i];
        h->tile[small] = h->tile[i];
        h->pri[i]  = fp;
        h->tile[i] = ft;
        i = small;
    }
    return best;
}

/* ---------------- territory ---------------- */

static void update_border(Env *e, int t) {
    int p = e->owner[t];
    if (p == 0) return;

    int nb[4];
    int n = neighbors(t, nb);
    int is_border = 0;
    for (int i = 0; i < n; i++) {
        if (e->owner[nb[i]] != p) { is_border = 1; break; }
    }

    if (is_border) ts_add(&e->players[p].border, t);
    else           ts_remove(&e->players[p].border, t);
}

/* The only territory mutation point. Five tiles touched, no rescans. */
static void conquer(Env *e, int p, int t) {
#ifdef DEBUG
    /* source throws on water/impassable; every sim caller already filters, so
       this only exists to catch a caller that stops filtering */
    if (e->terrain[t] == 0) {
        printf("CONQUER WATER: p%d tile %d\n", p, t);
        exit(1);
    }
#endif
    int prev = e->owner[t];
    if (prev == p) return;

    if (prev != 0) {
        ts_remove(&e->players[prev].tiles, t);
        ts_remove(&e->players[prev].border, t);
    }

    e->owner[t] = p;
    ts_add(&e->players[p].tiles, t);
    /* alive is a cached "owns at least one tile" (source isAlive() is exactly
       numTilesOwned() > 0), kept as a field only so player_tick/bot_tick/
       annex_tick can skip dead seats. Maintained in BOTH directions here,
       which is the only place territory moves — clearing it on loss alone left
       it wrong, and annexation can empty a player without ever going through
       attack_tick. */
    e->players[p].alive = 1;

    update_border(e, t);
    int nb[4];
    int n = neighbors(t, nb);
    for (int i = 0; i < n; i++)
        update_border(e, nb[i]);

    e->last_tile_change[p] = e->ticks;
    if (prev != 0) e->last_tile_change[prev] = e->ticks;

    if (prev != 0 && e->players[prev].tiles.count == 0)
        e->players[prev].alive = 0;
}

static void players_reset(Env *e) {
    for (int p = 0; p < MAXP; p++) {
        ts_init(&e->players[p].tiles);
        ts_init(&e->players[p].border);
        e->players[p].troops = 0.0f;
        e->players[p].alive  = 0;   /* nobody owns tiles until spawn_place runs */
        e->is_bot[p]         = 1;
        /* source: lastCalc = ticks + simpleHash(id) % ticksPerClusterCalc.
           Any fixed spread works; this one is deterministic per seat. */
        e->last_calc[p]        = (long)(p * 7 % ANNEX_PERIOD);
        e->last_tile_change[p] = 0;
    }
    memset(e->owner, 0, sizeof(e->owner));
}

/* ---------------- economy (spec 8) ---------------- */

/* The +50000 floor is spec 14 item 2, NOT applied — implemented as written so
   weird balance stays attributable to the constant rather than to the code. It
   dominates at 48x48 (tiles^0.6*1000 is ~10k at spawn size), which is exactly
   why start troops are coupled to it. */
static float max_troops(Env *e, int p) {
    float tiles = (float)e->players[p].tiles.count;
    float m = 2.0f * (powf(tiles, 0.6f) * 1000.0f + 50000.0f);
    if (e->is_bot[p]) m /= 3.0f;
    return m;
}

static float start_troops(Env *e, int p) {
    return e->is_bot[p] ? START_TROOPS_BOT : START_TROOPS_HUMAN;
}

static void player_tick(Env *e, int p) {
    if (!e->players[p].alive) return;
    float max = max_troops(e, p);
    float troops = e->players[p].troops;
    /* source order: base rate, then the capacity ratio, then the bot handicap.
       The ratio goes negative when max shrinks below current troops (territory
       loss), which is load-bearing — troops decay toward the new cap. */
    float add = 10.0f + powf(troops, 0.73f) / 4.0f;
    add *= (1.0f - troops / max);
    if (e->is_bot[p]) add *= 0.5f;
    troops += add;
    if (troops > max) troops = max;
    e->players[p].troops = troops;
}

/* ---------------- attack ---------------- */

static int find_free_slot(Env *e) {
    for (int i = 0; i < MAXATK; i++)
        if (e->attacks[i].active == 0) return i;
    return -1;
}

/* Conquest priority (spec 6), computed at ENQUEUE time using the tick counter
   at enqueue. numOwnedByMe >= 2 on plains drives the multiplier <= 0, so
   pockets and concavities fill first and fronts smooth out. + ticks makes
   older enqueues win ties across ticks. */
static void atk_push(Env *e, Attack *a, int t) {
    float mag;
    if      (e->terrain[t] == 1) mag = 1.0f;
    else if (e->terrain[t] == 2) mag = 1.5f;
    else if (e->terrain[t] == 3) mag = 2.0f;
    else                         mag = 0.0f;

    int nb[4];
    int n = neighbors(t, nb);
    int own = 0;
    for (int i = 0; i < n; i++)
        if (e->owner[nb[i]] == a->attacker) own++;

    float pri = (rng_int(e, 0, 7) + 10) * (1.0f - own*0.5f + mag/2.0f) + e->ticks;
    heap_push(e, &a->heap, t, pri);
}

/* Order is load-bearing: deduct -> cancel -> combine -> find slot -> assign.
   Both loops mutate the local `troops`, and combination can free a slot. */
static void attack_start(Env *e, int attacker, int target, float troops) {
    if (attacker == target) return;

    if (troops > e->players[attacker].troops) troops = e->players[attacker].troops;
    if (troops < 1.0f) return;
    e->players[attacker].troops -= troops;

    /* Cancellation (spec 5.4): mutual attacks annihilate pairwise, larger
       survives with the difference. */
    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].attacker != target || e->attacks[i].target != attacker) continue;
        if (e->attacks[i].troops > troops) {
            e->attacks[i].troops -= troops;
            return;
        }
        troops -= e->attacks[i].troops;
        e->attacks[i].active = 0;
        /* not cosmetic: without it a later mutual match does
           attacks[i].troops -= troops with negative troops, which ADDS troops
           to the enemy attack */
        if (troops <= 0.0f) return;
    }

    /* Combination (spec 5.5): a second outgoing attack on the same target
       absorbs the first. This is what supplies troop commitment for the action
       space, so it is a prerequisite rather than a fidelity nicety. */
    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].attacker != attacker || e->attacks[i].target != target) continue;
        troops += e->attacks[i].troops;
        e->attacks[i].active = 0;
    }

    if (troops < 1.0f) return;

    int i = find_free_slot(e);
    if (i < 0) {
        /* Deliberate divergence: source has an unbounded attack list. Refund
           rather than leak, since the deduction happened at the top. */
        e->players[attacker].troops += troops;
#ifdef DEBUG
        printf("attack_start: no free slot (a=%d t=%d troops=%.1f)\n",
               attacker, target, troops);
#endif
        return;
    }

    Attack *a = &e->attacks[i];
    a->active   = 1;
    a->attacker = attacker;
    a->target   = target;
    a->troops   = troops;

    heap_init(&a->heap);

    /* Seed via refresh: walk ALL the attacker's border tiles and enqueue every
       4-neighbor that is target-owned passable land. One Attack = one
       (attacker, target) pair spanning every shared front. */
    TileSet *b = &e->players[attacker].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            if (e->owner[nb[k]] == target && e->terrain[nb[k]] != 0)
                atk_push(e, a, nb[k]);
        }
    }
}

/* handleDeadDefender: fires after every conquest against a player under the
   threshold, transferring their ENTIRE remaining territory. Iterates the
   target's TileSet DESCENDING — ts_remove swaps the last element into the
   freed slot, so descending means that element is already visited; ascending
   skips unvisited tiles. */
static void dead_defender(Env *e, int attacker, int target) {
    if (target == 0) return;

    for (int pass = 0; pass < 100; pass++) {
        int progress = 0;
        TileSet *ts = &e->players[target].tiles;

        for (int i = ts->count - 1; i >= 0; i--) {
            int t = ts->tiles[i];
            int nb[4];
            int n = neighbors(t, nb);
            int taker = 0;
            for (int k = 0; k < n; k++) {
                int o = e->owner[nb[k]];
                if (o == attacker) { taker = attacker; break; }
                if (o != 0 && o != target && taker == 0) taker = o;
            }
            if (taker != 0) { conquer(e, taker, t); progress = 1; }
        }

        if (!progress || e->players[target].tiles.count == 0) break;
    }
}

static void attack_tick(Env *e, Attack *a) {
    /* Budget computed ONCE per tick. frontier proxy is heap.count, which
       overcounts lazy-deleted duplicates; the exact per-attack TileSet is a
       standing Phase 2 decision. */
    float frontier = (float)a->heap.count + rng_int(e, 0, 5);
    float budget;
    if (a->target == 0) {
        budget = frontier * 2.0f;
    } else {
        float def = e->players[a->target].troops;
        /* JS divides by zero to Infinity and clamps to 0.5; C would be UB, so
           branch straight to the 0.5 arm */
        if (def <= 0.0f) budget = 0.5f * frontier * 3.0f;
        else budget = within((5.0f*a->troops / def) * 2.0f, 0.01f, 0.5f) * frontier * 3.0f;
    }

    while (budget > 0.0f) {
        /* checked at the TOP of each iteration: attack dies, nothing returned */
        if (a->troops < 1.0f) { a->active = 0; return; }

        int t = heap_pop(&a->heap);
        if (t < 0) {
            /* heap empty -> retreat: survivors returned, no malus for
               auto-exhaustion. Distinct from the troops<1 death above. */
            e->players[a->attacker].troops += a->troops;
            a->active = 0;
            return;
        }

        /* validity checks, none consuming budget — the lazy-deletion payoff */
        if (e->owner[t] != a->target) continue;
        if (e->terrain[t] == 0) continue;
        int nb[4];
        int n = neighbors(t, nb);
        int on_border = 0;
        for (int i = 0; i < n; i++) {
            if (e->owner[nb[i]] == a->attacker) { on_border = 1; break; }
        }
        if (!on_border) continue;

        /* addNeighbors happens BEFORE the conquest (source order) */
        for (int k = 0; k < n; k++) {
            if (e->owner[nb[k]] == a->target && e->terrain[nb[k]] != 0)
                atk_push(e, a, nb[k]);
        }

        float mag, speed;
        if      (e->terrain[t] == 2) { mag = 100.0f; speed = 20.0f; }
        else if (e->terrain[t] == 3) { mag = 120.0f; speed = 25.0f; }
        else                         { mag = 80.0f;  speed = 16.5f; }

        float atk_loss, cost;
        if (a->target == 0) {
            /* Bot attackers pay half the terra nullius toll. Terra nullius is
               not a Bot, so the 0.7 modifier below never applies here. */
            atk_loss = e->is_bot[a->attacker] ? mag / 10.0f : mag / 5.0f;
            cost = within(2000.0f * (speed > 10.0f ? speed : 10.0f) / a->troops, 5.0f, 100.0f);
        } else {
            /* Human/Nation attacking a Bot takes 30% less loss. Both sides must
               be players, which is why this sits inside the vs-player branch. */
            if (!e->is_bot[a->attacker] && e->is_bot[a->target]) mag *= 0.7f;

            float def_troops = e->players[a->target].troops;
            int   def_tiles  = e->players[a->target].tiles.count;
            float def_loss = (def_tiles > 0) ? def_troops / def_tiles : 0.0f;
            float cur_loss = within(def_troops / a->troops, 0.6f, 2.0f) * mag * 0.8f;
            float alt_loss = 1.3f * def_loss * (mag / 100.0f);
            atk_loss = 0.6f*cur_loss + 0.4f*alt_loss;
            cost = within(def_troops / (5.0f * a->troops), 0.2f, 1.5f) * speed;
            /* source removeTroops clamps at zero; powf(negative, 0.73) in
               player_tick would be NaN otherwise */
            e->players[a->target].troops -= def_loss;
            if (e->players[a->target].troops < 0.0f) e->players[a->target].troops = 0.0f;
        }

        budget   -= cost;
        a->troops -= atk_loss;
        conquer(e, a->attacker, t);

        if (a->target != 0 && e->players[a->target].tiles.count < WIPE_TILES)
            dead_defender(e, a->attacker, a->target);
    }
}

/* ---------------- annexation (spec 9) ----------------

   Source: PlayerExecution.removeClusters. Three things differ from what an
   earlier draft of the spec said:

   1. THE INSCRIPTION TEST IS THE OTHER WAY AROUND. Util.inscribed(outer, inner)
      is called as inscribed(enemyBox, clusterBox), so the ENEMY bounding box
      must CONTAIN the cluster's, not the reverse. Under the reverse reading the
      rule can never fire: a ring of enemy tiles enclosing a cluster always
      extends one tile past it on every side. The correct reading is what makes
      the rule mean "the enemy wraps around us."

   2. removeCluster gates on isEnclosed(firstTile) before transferring anything.
      The surround checks look at one border component, but the fill hands over
      the whole territory that component sits on. Every hole in a territory
      gives it another border component, so a cluster wrapped around a hole in
      the middle of a wide-open empire passes the surround test; without this
      gate that empire changes hands.

   3. isOceanShore (largest-cluster rule) and isShore (every other cluster)
      collapse to one predicate here — no lakes, no shoreline bit.

   Cost is O(border), not O(territory): the cluster fill walks only the
   player's border set, and last_tile_change skips players who haven't moved.
   A territory with a hole is ONE connected territory but TWO border components
   (outer perimeter and the rim of the hole), which is exactly the distinction
   the rule needs. */

/* largest != 0 selects surroundedBySamePlayer (every 4-neighbor owned, exactly
   one distinct enemy); otherwise isSurrounded (unowned neighbors allowed, any
   number of enemies). Both require the enemy box to contain the cluster box. */
static int annex_surrounded(Env *e, int p, const int *tiles, int n, int largest) {
    int cminx = W, cmaxx = -1, cminy = H, cmaxy = -1;
    int eminx = W, emaxx = -1, eminy = H, emaxy = -1;
    int seen[MAXP];
    int distinct = 0;
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < n; i++) {
        int t = tiles[i];
        int x = rx(t), y = ry(t);
        if (x < cminx) cminx = x;
        if (x > cmaxx) cmaxx = x;
        if (y < cminy) cminy = y;
        if (y > cmaxy) cmaxy = y;

        if (is_shore(e, t) || on_map_edge(t)) return 0;

        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            int o = e->owner[u];
            if (o == p) continue;
            if (o == 0) {
                if (largest) return 0;   /* unowned neighbor breaks full surround */
                continue;
            }
            if (!seen[o]) { seen[o] = 1; distinct++; }
            int ux = rx(u), uy = ry(u);
            if (ux < eminx) eminx = ux;
            if (ux > emaxx) emaxx = ux;
            if (uy < eminy) eminy = uy;
            if (uy > emaxy) emaxy = uy;
        }
        /* source re-checks enemies.size !== 1 after EVERY tile, so a second
           distinct enemy aborts early rather than at the end — cheaper and
           identical in result */
        if (largest && distinct != 1) return 0;
    }

    if (largest && distinct != 1) return 0;
    if (!largest && distinct == 0) return 0;

    return eminx <= cminx && eminy <= cminy && emaxx >= cmaxx && emaxy >= cmaxy;
}

/* Can the territory reachable from start escape to water or the map edge,
   walking our own tiles AND unclaimed land but not other players'? Unclaimed
   land is walked through, not treated as an exit — a crater inside our land is
   a hole, not a way out. */
static int annex_enclosed(Env *e, int p, int start) {
    e->ff_gen++;
    if (e->ff_gen == 0) { memset(e->ff_visited, 0, sizeof(e->ff_visited)); e->ff_gen = 1; }

    int sp = 0;
    e->ff_visited[start] = e->ff_gen;
    e->ff_stack[sp++] = start;

    while (sp > 0) {
        int t = e->ff_stack[--sp];
        if (on_map_edge(t)) return 0;
        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            if (e->ff_visited[u] == e->ff_gen) continue;
            int o = e->owner[u];
            if (o != 0 && o != p) continue;                 /* someone else's tile: wall */
            if (o == 0 && e->terrain[u] == 0) return 0;      /* open water: a way out */
            e->ff_visited[u] = e->ff_gen;
            e->ff_stack[sp++] = u;
        }
    }
    return 1;
}

/* The enemy running the largest attack on p, restricted to enemies bordering
   this cluster; failing that, the enemy bordering it on the most tiles.
   Deliberate divergence: ties break by player id rather than cluster-traversal
   order, which is an artifact. */
static int annex_capturer(Env *e, int p, const int *tiles, int n) {
    int cnt[MAXP];
    memset(cnt, 0, sizeof(cnt));
    int any = 0;

    for (int i = 0; i < n; i++) {
        int nb[4];
        int k = neighbors(tiles[i], nb);
        for (int j = 0; j < k; j++) {
            int o = e->owner[nb[j]];
            if (o == 0 || o == p) continue;
            cnt[o]++;
            any = 1;
        }
    }
    if (!any) return 0;

    int best = 0;
    float best_troops = 0.0f;
    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].target != p) continue;
        if (cnt[e->attacks[i].attacker] == 0) continue;
        if (e->attacks[i].troops > best_troops) {
            best_troops = e->attacks[i].troops;
            best = e->attacks[i].attacker;
        }
    }
    if (best != 0) return best;

    int mode = 0, mode_cnt = 0;
    for (int q = 1; q < MAXP; q++)
        if (cnt[q] > mode_cnt) { mode_cnt = cnt[q]; mode = q; }
    return mode;
}

static void annex_remove(Env *e, int p, const int *tiles, int n) {
    /* an earlier removal this tick may already have taken these tiles */
    for (int i = 0; i < n; i++)
        if (e->owner[tiles[i]] != p) return;

    int cap = annex_capturer(e, p, tiles, n);
    if (cap == 0) return;
    if (!annex_enclosed(e, p, tiles[0])) return;

    e->ff_gen++;
    if (e->ff_gen == 0) { memset(e->ff_visited, 0, sizeof(e->ff_visited)); e->ff_gen = 1; }

    int sp = 0, ntake = 0;
    e->ff_visited[tiles[0]] = e->ff_gen;
    e->ff_stack[sp++] = tiles[0];

    while (sp > 0) {
        int t = e->ff_stack[--sp];
        e->ff_take[ntake++] = t;
        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            if (e->ff_visited[u] == e->ff_gen) continue;
            if (e->owner[u] != p) continue;
            e->ff_visited[u] = e->ff_gen;
            e->ff_stack[sp++] = u;
        }
    }

    /* collect first, conquer second — the fill's include-test reads owner, and
       conquering inside the loop terminates it early against its own results */
    for (int i = 0; i < ntake; i++) conquer(e, cap, e->ff_take[i]);

    e->annex_events++;
    e->annex_tiles_moved += ntake;
}

static void annex_tick(Env *e, int p) {
    if (!e->players[p].alive) return;
    TileSet *b = &e->players[p].border;
    if (b->count == 0) return;

    if (!(e->ticks - e->last_calc[p] > ANNEX_PERIOD ||
          e->players[p].tiles.count < ANNEX_TILES)) return;
    if (e->last_tile_change[p] < e->last_calc[p]) return;
    e->last_calc[p] = e->ticks;

    e->cl_gen++;
    if (e->cl_gen == 0) { memset(e->cl_visited, 0, sizeof(e->cl_visited)); e->cl_gen = 1; }

    /* 8-connected components of the BORDER SET */
    int ncomp = 0, ntot = 0;
    for (int i = 0; i < b->count; i++) {
        int s = b->tiles[i];
        if (e->cl_visited[s] == e->cl_gen) continue;

        e->cl_start[ncomp] = ntot;
        int sp = 0;
        e->cl_visited[s] = e->cl_gen;
        e->cl_stack[sp++] = s;

        while (sp > 0) {
            int t = e->cl_stack[--sp];
            e->cl_comp[ntot++] = t;
            int nb[8];
            int k = neighbors8(t, nb);
            for (int j = 0; j < k; j++) {
                int u = nb[j];
                if (e->cl_visited[u] == e->cl_gen) continue;
                if (!ts_has(b, u)) continue;
                e->cl_visited[u] = e->cl_gen;
                e->cl_stack[sp++] = u;
            }
        }
        e->cl_size[ncomp] = ntot - e->cl_start[ncomp];
        ncomp++;
    }
    if (ncomp == 0) return;

    int largest = 0;
    for (int i = 1; i < ncomp; i++)
        if (e->cl_size[i] > e->cl_size[largest]) largest = i;

    if (annex_surrounded(e, p, &e->cl_comp[e->cl_start[largest]], e->cl_size[largest], 1))
        annex_remove(e, p, &e->cl_comp[e->cl_start[largest]], e->cl_size[largest]);

    for (int i = 0; i < ncomp; i++) {
        if (i == largest) continue;
        if (!e->players[p].alive) return;
        if (annex_surrounded(e, p, &e->cl_comp[e->cl_start[i]], e->cl_size[i], 0))
            annex_remove(e, p, &e->cl_comp[e->cl_start[i]], e->cl_size[i]);
    }
}

/* ---------------- scripted bot driver (spec 11) ---------------- */

/* All randInt here are nextInt, i.e. max EXCLUSIVE: attack_rate 40..79,
   trigger 0.50..0.59, reserve 0.30..0.39, expand 0.10..0.19. */
static void bots_init(Env *e) {
    for (int i = 1; i < MAXP; i++) {
        e->bots[i].attack_rate   = rng_int(e, 40, 80);
        e->bots[i].attack_off    = rng_below(e, e->bots[i].attack_rate);
        e->bots[i].trigger_ratio = rng_int(e, 50, 60) / 100.0f;
        e->bots[i].reserve_ratio = rng_int(e, 30, 40) / 100.0f;
        e->bots[i].expand_ratio  = rng_int(e, 10, 20) / 100.0f;
        e->bots[i].neighbors_tn  = 1;
    }
}

static int has_tn_neighbor(Env *e, int p) {
    TileSet *b = &e->players[p].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            if (e->owner[nb[k]] == 0 && e->terrain[nb[k]] != 0)
                return 1;
        }
    }
    return 0;
}

static int bordering_players(Env *e, int p, int *out) {
    int count = 0;
    TileSet *b = &e->players[p].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            int o = e->owner[nb[k]];
            if (o != 0 && o != p) {
                int found = 0;
                for (int i = 0; i < count; i++) {
                    if (out[i] == o) { found = 1; break; }
                }
                if (!found) out[count++] = o;
            }
        }
    }
    return count;
}

/* Largest active attack aimed at p, returning its attacker (0 if none).
   Non-bots ignore bot attackers (findIncomingAttackPlayer); bots count
   everyone. Only bots call this today, but the agent seat will. */
static int largest_incoming_attacker(Env *e, int p) {
    int best = 0;
    float best_troops = 0.0f;
    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].target != p) continue;
        if (!e->is_bot[p] && e->is_bot[e->attacks[i].attacker]) continue;
        if (e->attacks[i].troops > best_troops) {
            best_troops = e->attacks[i].troops;
            best = e->attacks[i].attacker;
        }
    }
    return best;
}

/* attackRandomTarget. The trigger gate comes FIRST — source calls
   hasTriggerRatioTroops at the top and returns, so "forced" retaliation only
   skips shouldAttack, which returns true unconditionally for a Bot attacker
   and is therefore a no-op. Do not read force=true as bypassing the gate. */
static void bot_attack_random(Env *e, int p) {
    if (e->players[p].troops < e->bots[p].trigger_ratio * max_troops(e, p)) return;

    float send = e->players[p].troops - max_troops(e, p) * e->bots[p].reserve_ratio;
    if (send < 1.0f) return;   /* sendAttack fails identically for every target */

    int r = largest_incoming_attacker(e, p);
    if (r != 0) {
        attack_start(e, p, r, send);
        return;
    }

    int cand[MAXP];
    int count = bordering_players(e, p, cand);
    if (count == 0) return;

    /* shuffleArray (Fisher-Yates), then take the first candidate that goes
       through */
    for (int i = count - 1; i > 0; i--) {
        int j = rng_below(e, i + 1);
        int tmp = cand[i]; cand[i] = cand[j]; cand[j] = tmp;
    }
    for (int i = 0; i < count; i++) {
        int q = cand[i];
        /* chance(2): 50% skip per Human/Nation candidate; bots always attackable */
        if (!e->is_bot[q] && rng_below(e, 2) == 0) continue;
        attack_start(e, p, q, send);
        return;
    }
}

static void bot_tick(Env *e, int p) {
    if (!e->players[p].alive) return;
    if (e->ticks % e->bots[p].attack_rate != e->bots[p].attack_off) return;

    if (e->bots[p].neighbors_tn) {
        if (has_tn_neighbor(e, p)) {
            float send = e->players[p].troops - max_troops(e, p) * e->bots[p].expand_ratio;
            if (send >= 1.0f) {
                attack_start(e, p, 0, send);
                return;
            }
            /* too poor to expand: source FALLS THROUGH to attackRandomTarget
               rather than burning the decision tick. An early return here
               silently halves bot activity. */
        } else {
            e->bots[p].neighbors_tn = 0;   /* cleared permanently, then fall through */
        }
    }
    bot_attack_random(e, p);
}

/* ---------------- win condition (spec 10) ---------------- */

static int win_check(Env *e) {
    int best = 0;
    int best_p = 0;
    for (int p = 1; p < MAXP; p++) {
        if (e->players[p].tiles.count > best) {
            best = e->players[p].tiles.count;
            best_p = p;
        }
    }
    if ((float)best / e->land_tiles > 0.8f) return best_p;
    return 0;
}

/* ---------------- spawn placement (spec 12) ---------------- */

static int spawn_disk_ok(Env *e, int cx, int cy) {
    for (int dy = -SPAWN_RADIUS; dy <= SPAWN_RADIUS; dy++) {
        for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; dx++) {
            if (dx*dx + dy*dy > SPAWN_RADIUS*SPAWN_RADIUS) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= W || y < 0 || y >= H) return 0;
            int t = ref(x, y);
            if (e->terrain[t] == 0) return 0;
            if (e->owner[t]  != 0) return 0;
        }
    }
    return 1;
}

/* Returns 1 on success. Filter order is cheap-first; the 81-cell disk check
   runs last. */
static int spawn_place(Env *e, int p) {
    for (int attempt = 0; attempt < SPAWN_TRIES; attempt++) {
        int cx = rng_below(e, W), cy = rng_below(e, H);
        int c  = ref(cx, cy);

        if (e->terrain[c] == 0 || e->owner[c] != 0) continue;

        /* source rejects border tiles; for an unowned tile that means any
           4-neighbor already belongs to someone */
        int nb[4];
        int n = neighbors(c, nb), touching = 0;
        for (int k = 0; k < n; k++) if (e->owner[nb[k]] != 0) { touching = 1; break; }
        if (touching) continue;

        /* min-distance is a preference, not a constraint: dropped after
           SPAWN_RELAX so a cramped map degrades to packed spawns instead of
           hanging. Source does the same. */
        if (attempt < SPAWN_RELAX) {
            int too_close = 0;
            for (int q = 1; q < MAXP; q++) {
                if (q == p || e->spawn_center[q] < 0) continue;
                int dx = rx(e->spawn_center[q]) - cx;
                int dy = ry(e->spawn_center[q]) - cy;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx + dy < SPAWN_MIN_DIST) { too_close = 1; break; }
            }
            if (too_close) continue;
        }

        if (!spawn_disk_ok(e, cx, cy)) continue;

        for (int dy = -SPAWN_RADIUS; dy <= SPAWN_RADIUS; dy++)
            for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; dx++) {
                if (dx*dx + dy*dy > SPAWN_RADIUS*SPAWN_RADIUS) continue;
                conquer(e, p, ref(cx + dx, cy + dy));
            }
        e->spawn_center[p] = c;
        return 1;
    }
    return 0;
}

/* ---------------- episode lifecycle ---------------- */

/* Resets everything an episode owns. Does NOT touch rng_state (the stream
   continues across episodes) and does NOT clear the cross-episode counters. */
static void sim_reset(Env *e) {
    /* BEFORE spawn_place, not after: conquer stamps last_tile_change with the
       tick counter, and a leftover value from the previous episode would make
       every player look freshly-changed on tick 0 */
    e->ticks = 0;
    fill_terrain(e);                /* fresh map per episode */
    players_reset(e);
    bots_init(e);
    for (int i = 0; i < MAXATK; i++) e->attacks[i].active = 0;
    for (int p = 0; p < MAXP; p++) e->spawn_center[p] = -1;

    for (int p = 1; p < MAXP; p++) {
        if (!spawn_place(e, p)) {   /* no room left: player sits out this episode */
            e->players[p].alive = 0;
            e->spawn_failures++;
            continue;
        }
        e->players[p].troops = start_troops(e, p);
    }
}

/* Zeroes an Env completely. Call once before first use; sim_reset is the
   per-episode entry point. */
static void sim_init(Env *e, unsigned int seed) {
    memset(e, 0, sizeof(*e));
    rng_seed(e, seed);
    e->cl_gen = 0;
    e->ff_gen = 0;
}

#ifdef DEBUG
static void check_borders(Env *e);
#else
#define check_borders(e) ((void)0)
#endif

/* One tick, in spec 2 order. Returns the winner (0 if none) so callers can
   stop; win check runs every 10 ticks as in source. */
static int sim_tick(Env *e) {
    e->ticks++;
    for (int p = 1; p < MAXP; p++) bot_tick(e, p);
    for (int i = 0; i < MAXATK; i++)
        if (e->attacks[i].active) attack_tick(e, &e->attacks[i]);
    /* spec 2: per player, troop growth THEN the annexation check */
    for (int p = 1; p < MAXP; p++) { player_tick(e, p); annex_tick(e, p); }
    if (e->ticks % 50 == 0) check_borders(e);
    if (e->ticks % 10 == 0) return win_check(e);
    return 0;
}

static void sim_run(Env *e, int nticks) {
    for (int tick = 0; tick < nticks; tick++)
        if (sim_tick(e)) return;
}

/* ---------------- invariant checks (debug builds only) ---------------- */

#ifdef DEBUG
static void ts_check(TileSet *s) {
    for (int i = 0; i < s->count; i++) {
        int t = s->tiles[i];
        if (s->pos[t] != i) {
            printf("BROKEN: tiles[%d]=%d but pos[%d]=%d\n", i, t, t, s->pos[t]);
            exit(1);
        }
    }
}

static void check_borders(Env *e) {
    for (int t = 0; t < N; t++) {
        int p = e->owner[t];
        if (p == 0) continue;
        int nb[4], n = neighbors(t, nb), should = 0;
        for (int i = 0; i < n; i++) if (e->owner[nb[i]] != p) should = 1;
        if (ts_has(&e->players[p].border, t) != should) {
            printf("BORDER BROKEN: tile %d owner %d should=%d has=%d\n",
                   t, p, should, ts_has(&e->players[p].border, t));
            exit(1);
        }
    }
    for (int p = 1; p < MAXP; p++) {
        for (int i = 0; i < e->players[p].border.count; i++) {
            int t = e->players[p].border.tiles[i];
            if (e->owner[t] != p) {
                printf("PHANTOM: p%d border has tile %d owned by %d\n", p, t, e->owner[t]);
                exit(1);
            }
        }
        /* alive must track tile ownership exactly: annexation can empty a
           player through a path that does not go via attack_tick */
        if ((e->players[p].tiles.count == 0) != (e->players[p].alive == 0)) {
            printf("ALIVE BROKEN: p%d tiles=%d alive=%d\n",
                   p, e->players[p].tiles.count, e->players[p].alive);
            exit(1);
        }
        if (e->players[p].troops < 0.0f) {
            printf("TROOPS BROKEN: p%d troops=%f\n", p, (double)e->players[p].troops);
            exit(1);
        }
        if (e->players[p].troops != e->players[p].troops) {
            printf("TROOPS NaN: p%d\n", p);
            exit(1);
        }
    }
}
#endif

/* ---------------- tests (debug builds only) ---------------- */

#ifdef DEBUG

/* Tests own their Env explicitly. Heap-allocated because sizeof(Env) is large
   enough that a stack local would blow the default stack. */
static Env *test_env(unsigned int seed) {
    Env *e = (Env*)malloc(sizeof(Env));
    if (!e) { printf("test_env: out of memory\n"); exit(1); }
    sim_init(e, seed);
    return e;
}

static TileSet test_set;
static int test_present[N];

static void ts_test(void) {
    ts_init(&test_set);
    memset(test_present, 0, sizeof(test_present));
    int n_present = 0;

    for (int step = 0; step < 200000; step++) {
        int t = rand() % N;
        if (rand() % 2) {
            ts_add(&test_set, t);
            if (!test_present[t]) { test_present[t] = 1; n_present++; }
        } else {
            ts_remove(&test_set, t);
            if (test_present[t])  { test_present[t] = 0; n_present--; }
        }
        if (test_set.count != n_present) {
            printf("BROKEN: count=%d expected=%d\n", test_set.count, n_present);
            exit(1);
        }
        if (ts_has(&test_set, t) != test_present[t]) {
            printf("BROKEN: has(%d)=%d expected=%d\n",
                   t, ts_has(&test_set, t), test_present[t]);
            exit(1);
        }
        ts_check(&test_set);
    }
    printf("tileset ok\n");
}

static void conquer_test(void) {
    Env *e = test_env(1);
    memset(e->terrain, 1, sizeof(e->terrain));   /* guard in conquer rejects water */
    players_reset(e);
    for (int step = 0; step < 50000; step++) {
        int p = 1 + rand() % (MAXP - 1);
        int t = rand() % N;
        conquer(e, p, t);
        if (step % 500 == 0) check_borders(e);
    }
    check_borders(e);

    int total = 0;
    for (int p = 1; p < MAXP; p++) total += e->players[p].tiles.count;
    printf("conquer ok: %d tiles owned, p1 has %d tiles / %d border\n",
           total, e->players[1].tiles.count, e->players[1].border.count);
    free(e);
}

static void fill_rect(Env *e, int p, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            conquer(e, p, ref(x, y));
}

static void blob_test(void) {
    Env *e = test_env(2);
    memset(e->terrain, 1, sizeof(e->terrain));
    players_reset(e);

    /* solid 12x12 rectangles so interior (non-border) tiles actually exist —
       catches a missing interior ts_remove in update_border */
    for (int p = 1; p <= 4; p++) {
        int ox = (p % 2) * 20 + 4, oy = (p / 3) * 20 + 4;
        fill_rect(e, p, ox, oy, 12, 12);
    }
    check_borders(e);
    if (e->players[1].tiles.count != 144 || e->players[1].border.count != 44) {
        printf("BLOB BROKEN: p1 tiles=%d border=%d (expect 144/44)\n",
               e->players[1].tiles.count, e->players[1].border.count);
        exit(1);
    }

    /* chew a 3x3 hole in the middle of p1 — catches a missing neighbor-update
       loop in conquer */
    fill_rect(e, 5, 28, 8, 3, 3);
    check_borders(e);
    if (e->players[1].tiles.count != 135 || e->players[1].border.count != 56) {
        printf("HOLE BROKEN: p1 tiles=%d border=%d (expect 135/56)\n",
               e->players[1].tiles.count, e->players[1].border.count);
        exit(1);
    }
    printf("blob/hole ok\n");
    free(e);
}

static void heap_test(void) {
    Env *e = test_env(3);
    Heap *h = (Heap*)malloc(sizeof(Heap));
    if (!h) { printf("heap_test: out of memory\n"); exit(1); }
    heap_init(h);

    int n_push = HEAPCAP;
    for (int i = 0; i < n_push; i++)
        heap_push(e, h, i % N, (float)(rand() % 100000));

    float prev = -1.0f;
    int popped = 0;
    while (h->count > 0) {
        float p = h->pri[0];
        int t = heap_pop(h);
        if (t < 0) { printf("HEAP BROKEN: pop returned -1 with count>0\n"); exit(1); }
        if (p < prev) { printf("HEAP BROKEN: %f came out after %f\n", (double)p, (double)prev); exit(1); }
        prev = p;
        popped++;
    }
    if (popped != n_push) { printf("HEAP BROKEN: popped %d of %d\n", popped, n_push); exit(1); }

    heap_init(h);
    for (int step = 0; step < 100000; step++) {
        if (h->count == 0 || rand() % 2)
            heap_push(e, h, rand() % N, (float)(rand() % 1000));
        else
            heap_pop(h);
        for (int i = 0; i < h->count; i++) {
            int l = 2*i+1, r = 2*i+2;
            if (l < h->count && h->pri[i] > h->pri[l]) {
                printf("HEAP BROKEN: node %d > left child\n", i); exit(1);
            }
            if (r < h->count && h->pri[i] > h->pri[r]) {
                printf("HEAP BROKEN: node %d > right child\n", i); exit(1);
            }
        }
    }
    printf("heap ok\n");
    free(h);
    free(e);
}

/* Forces the schedule so annex_tick actually runs this call. */
static void annex_force(Env *e, int p) {
    e->last_calc[p] = 0;
    e->last_tile_change[p] = e->ticks;
    annex_tick(e, p);
}

static void annex_test(void) {
    Env *e = test_env(4);
    memset(e->terrain, 1, sizeof(e->terrain));   /* all land: isolate the geometry */

    /* --- positive: p2 is a 3x3 enclave inside p1's 12x12 --- */
    players_reset(e);
    e->ticks = 100;
    fill_rect(e, 1, 10, 10, 12, 12);
    fill_rect(e, 2, 15, 15,  3,  3);
    check_borders(e);
    if (e->players[1].tiles.count != 135 || e->players[2].tiles.count != 9) {
        printf("ANNEX SETUP BROKEN: p1=%d p2=%d (expect 135/9)\n",
               e->players[1].tiles.count, e->players[2].tiles.count);
        exit(1);
    }

    long before = e->annex_events;
    annex_force(e, 2);
    check_borders(e);
    if (e->annex_events != before + 1) {
        printf("ANNEX BROKEN: enclave not annexed (events %ld -> %ld)\n",
               before, e->annex_events);
        exit(1);
    }
    if (e->players[2].tiles.count != 0 || e->players[2].alive != 0) {
        printf("ANNEX BROKEN: p2 tiles=%d alive=%d (expect 0/0)\n",
               e->players[2].tiles.count, e->players[2].alive);
        exit(1);
    }
    if (e->players[1].tiles.count != 144) {
        printf("ANNEX BROKEN: p1 tiles=%d (expect 144)\n", e->players[1].tiles.count);
        exit(1);
    }

    /* p1's own outer ring must survive the same pass: it is not surrounded */
    annex_force(e, 1);
    check_borders(e);
    if (e->players[1].tiles.count != 144) {
        printf("ANNEX BROKEN: p1 annexed itself away, tiles=%d\n",
               e->players[1].tiles.count);
        exit(1);
    }

    /* --- negative: two blobs sharing one front, neither is enclosed --- */
    players_reset(e);
    e->ticks = 100;
    fill_rect(e, 1, 10, 10, 12, 12);
    fill_rect(e, 2, 22, 10, 12, 12);
    check_borders(e);
    before = e->annex_events;
    annex_force(e, 1);
    annex_force(e, 2);
    check_borders(e);
    if (e->annex_events != before) {
        printf("ANNEX BROKEN: shared front annexed (events %ld -> %ld)\n",
               before, e->annex_events);
        exit(1);
    }
    if (e->players[1].tiles.count != 144 || e->players[2].tiles.count != 144) {
        printf("ANNEX BROKEN: shared front moved tiles p1=%d p2=%d\n",
               e->players[1].tiles.count, e->players[2].tiles.count);
        exit(1);
    }

    printf("annex ok\n");
    free(e);
}

static void attack_test(void) {
    Env *e = test_env(5);
    memset(e->terrain, 1, sizeof(e->terrain));   /* flat map: no terrain confound */
    e->land_tiles = N;
    players_reset(e);
    bots_init(e);
    for (int i = 0; i < MAXATK; i++) e->attacks[i].active = 0;

    for (int p = 1; p < MAXP; p++) {
        int ox = 4 + ((p-1) % 4) * 11;
        int oy = 6 + ((p-1) / 4) * 20;
        fill_rect(e, p, ox, oy, 5, 5);
        e->players[p].troops = start_troops(e, p);
    }
    check_borders(e);
    e->ticks = 0;

    for (int tick = 0; tick < 300; tick++) {
        sim_tick(e);
        check_borders(e);
    }
    printf("attack ok: final");
    for (int p = 1; p < MAXP; p++) printf(" p%d=%d", p, e->players[p].tiles.count);
    putchar('\n');
    free(e);
}

/* Two envs stepped interleaved must produce bit-identical results to the same
   two envs stepped separately. This is the test the refactor exists for: any
   surviving file-static map, counter, or generation stamp shows up here and
   nowhere else. */
static unsigned long env_hash(Env *e) {
    unsigned long h = 1469598103934665603UL;
    for (int t = 0; t < N; t++) {
        h = (h ^ e->owner[t]) * 1099511628211UL;
        h = (h ^ e->terrain[t]) * 1099511628211UL;
    }
    for (int p = 1; p < MAXP; p++) {
        unsigned int bits;
        float tr = e->players[p].troops;
        memcpy(&bits, &tr, 4);
        h = (h ^ bits) * 1099511628211UL;
        h = (h ^ (unsigned long)e->players[p].tiles.count) * 1099511628211UL;
        h = (h ^ (unsigned long)e->players[p].alive) * 1099511628211UL;
    }
    h = (h ^ (unsigned long)e->annex_events) * 1099511628211UL;
    h = (h ^ (unsigned long)e->ticks) * 1099511628211UL;
    return h;
}

#define ISO_ENVS    3
#define ISO_EPS     3
#define ISO_TICKS 800

static void isolation_test(void) {
    static const unsigned int seeds[ISO_ENVS] = {11, 22, 33};
    unsigned long alone[ISO_ENVS];

    /* each env run entirely on its own */
    for (int i = 0; i < ISO_ENVS; i++) {
        Env *e = test_env(seeds[i]);
        for (int ep = 0; ep < ISO_EPS; ep++) {
            sim_reset(e);
            sim_run(e, ISO_TICKS);
        }
        alone[i] = env_hash(e);
        free(e);
    }

    /* same seeds, same episode counts, but every env advanced one tick at a
       time in round-robin — the access pattern PufferLib's vec loop produces */
    Env *envs[ISO_ENVS];
    int ep_left[ISO_ENVS], tick_left[ISO_ENVS];
    for (int i = 0; i < ISO_ENVS; i++) {
        envs[i] = test_env(seeds[i]);
        sim_reset(envs[i]);
        ep_left[i] = ISO_EPS;
        tick_left[i] = ISO_TICKS;
    }
    for (;;) {
        int busy = 0;
        for (int i = 0; i < ISO_ENVS; i++) {
            if (ep_left[i] == 0) continue;
            busy = 1;
            if (tick_left[i] > 0 && !sim_tick(envs[i])) {
                tick_left[i]--;
                continue;
            }
            /* episode over (win or tick budget spent) */
            ep_left[i]--;
            if (ep_left[i] > 0) {
                sim_reset(envs[i]);
                tick_left[i] = ISO_TICKS;
            }
        }
        if (!busy) break;
    }
    for (int i = 0; i < ISO_ENVS; i++) {
        unsigned long h = env_hash(envs[i]);
        if (h != alone[i]) {
            printf("ISOLATION BROKEN: env %u alone %lx interleaved %lx\n",
                   seeds[i], alone[i], h);
            printf("  -> simulation state is still shared between envs\n");
            exit(1);
        }
        free(envs[i]);
    }
    printf("isolation ok (%d envs x %d episodes round-robin, bit-identical)\n",
           ISO_ENVS, ISO_EPS);
}

static void run_tests(void) {
    ts_test();
    conquer_test();
    blob_test();
    heap_test();
    annex_test();
    attack_test();
    isolation_test();
}
#else
static void run_tests(void) {}
#endif

/* ---------------- harness ---------------- */

static void hist_run(int episodes, int nticks, unsigned int seed) {
    Env *e = (Env*)malloc(sizeof(Env));
    if (!e) { printf("out of memory\n"); exit(1); }
    sim_init(e, seed);

    long wins = 0, total_len = 0, survivors = 0, elim = 0;
    for (int ep = 0; ep < episodes; ep++) {
        sim_reset(e);
        sim_run(e, nticks);
        if (e->ticks < nticks) wins++;
        total_len += e->ticks;
        for (int p = 1; p < MAXP; p++) {
            if (e->players[p].tiles.count > 0) survivors++;
            else elim++;
        }
    }
    printf("episodes %d: wins %ld (%.1f%%), mean length %.0f ticks, eliminated %.1f%%\n",
           episodes, wins, 100.0*wins/episodes, (double)total_len/episodes,
           100.0*elim/(elim+survivors));
    printf("annexations %ld (%.2f/ep), tiles moved %ld (%.1f/event)\n",
           e->annex_events, (double)e->annex_events/episodes, e->annex_tiles_moved,
           e->annex_events ? (double)e->annex_tiles_moved/e->annex_events : 0.0);
    printf("spawn failures %ld, heap peak %d/%d, heap drops %ld\n",
           e->spawn_failures, e->heap_peak, HEAPCAP, e->heap_full_drops);
    free(e);
}

static void bench(unsigned int seed) {
    Env *e = (Env*)malloc(sizeof(Env));
    if (!e) { printf("out of memory\n"); exit(1); }
    sim_init(e, seed);

    long total_ticks = 0;
    int episodes = 0;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (total_ticks < 2000000) {
        sim_reset(e);
        sim_run(e, 1000);
        total_ticks += 1000;
        episodes++;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%ld ticks in %d episodes, %.3f s -> %.0f ticks/sec\n",
           total_ticks, episodes, secs, total_ticks / secs);
    free(e);
}

int main(int argc, char **argv) {
    printf("sizeof(Env) = %zu bytes (%.2f MB)\n",
           sizeof(Env), sizeof(Env) / (1024.0*1024.0));
    run_tests();
    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        bench(42);
    } else {
        hist_run(300, 2000, 42);
    }
    (void)print_owner;
    return 0;
}