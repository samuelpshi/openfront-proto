#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define W 48
#define H 48
#define N (W*H)
#define MAXP 9

/* ---------------- map ---------------- */

unsigned char terrain[N];
unsigned short owner[N];
int land_tiles = 0;
long ticks = 0;

/* ---------------- rng ----------------
   Per-sim state, not global rand(). PufferLib runs thousands of envs in
   parallel and they cannot share a global stream; spec 12 calls this out for
   spawn placement specifically, but a half-migration leaves episodes
   correlated, so every call site moves. xorshift32: one multiply-free step,
   good enough for map/AI jitter, trivially seedable per env. */
unsigned int rng_state = 1;

void rng_seed(unsigned int s) { 
    rng_state = (s == 0) ? 1u : s; 
}

unsigned int rng_next(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* uniform in [0, n), n > 0 */
int rng_below(int n) { 
    return (int)(rng_next() % (unsigned int)n); 
}

/* Matches source's PseudoRandom.nextInt(min, max): min INCLUSIVE, max
   EXCLUSIVE. The spec claimed nextInt was inclusive both ends; the class
   comment in PseudoRandom.ts says otherwise, verified Aug 2026. Keeping the
   exclusive convention here means every nextInt(a, b) in the TS transcribes to
   rng_int(a, b) argument-for-argument, which kills a whole class of off-by-one
   transcription errors. Call sites below are verbatim from source. */
int rng_int(int lo, int hi) {
    if (hi <= lo) return lo;   /* nextInt(0,0) is 0 in JS; avoid mod-by-zero */
    return lo + rng_below(hi - lo);
}

int ref(int x, int y) { return y*W + x; }
int rx(int r) { return r % W; }
int ry(int r) { return r / W; }

float within(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int neighbors(int r, int *out) {
    int n = 0;
    if (ry(r) != 0)
        out[n++] = r-W;
    if (ry(r) != H-1)
        out[n++] = r+W;
    if (rx(r) != 0)
        out[n++] = r-1;
    if (rx(r) != W-1) 
        out[n++] = r+1;
    return n;
}

/* 8-connectivity, used only by the annexation cluster fill (spec 9 —
   forEachNeighborWithDiag). Writes up to 8 tiles. */
int neighbors8(int r, int *out) {
    /* offsets, not ref(x,y) per neighbor: the obvious dx/dy double loop costs
       eight multiplies per call and measured ~9% of total throughput once
       annexation was wired in. One div and one mod instead. */
    int x = r % W, y = r / W, n = 0;
    int up = (y != 0), dn = (y != H-1), lf = (x != 0), rt = (x != W-1);
    if (up) { if (lf) out[n++] = r-W-1; out[n++] = r-W; if (rt) out[n++] = r-W+1; }
    if (lf) out[n++] = r-1;
    if (rt) out[n++] = r+1;
    if (dn) { if (lf) out[n++] = r+W-1; out[n++] = r+W; if (rt) out[n++] = r+W+1; }
    return n;
}

int on_map_edge(int t) {
    int x = rx(t), y = ry(t);
    return x == 0 || x == W-1 || y == 0 || y == H-1;
}

/* Source distinguishes isOceanShore from isShore (ocean bit vs shoreline bit).
   We have neither — one water value, no lakes — so both collapse to "land tile
   touching water". */
int is_shore(int t) {
    if (terrain[t] == 0) return 0;
    int nb[4];
    int n = neighbors(t, nb);
    for (int i = 0; i < n; i++)
        if (terrain[nb[i]] == 0) return 1;
    return 0;
}

void fill_terrain(void) {
    land_tiles = 0;
    for (int r = 0; r < N; r++) {
        if (ry(r) == 0 || ry(r) == H-1 || rx(r) == 0 || rx(r) == W-1)
            terrain[r] = 0;
        else
            terrain[r] = 1;
    }

    for (int i = 0; i < 15; i++) {
        int cx  = rng_below(W);
        int cy  = rng_below(H);
        int rad = 4 + rng_below(8);
        int inner = rad * 2 / 3;

        for (int y = cy-rad; y <= cy+rad; y++) {
            for (int x = cx-rad; x <= cx+rad; x++) {
                if (x < 0 || x >= W || y < 0 || y >= H) continue;
                int r = ref(x, y);
                if (terrain[r] == 0) continue;
                int dx = x - cx;
                int dy = y - cy;
                int d2 = dx*dx + dy*dy;
                if (d2 <= rad*rad)
                    terrain[r] = (d2 <= inner*inner) ? 3 : 2;
            }
        }
    }
    for (int r = 0; r < N; r++)
        if (terrain[r] != 0) land_tiles++;
}

void print_map(void) {
    static const char chars[] = ".-~^";
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) putchar(chars[terrain[ref(x, y)]]);
        putchar('\n');
    }
}

void print_owner(void) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r = ref(x, y);
            if (terrain[r] == 0)    putchar('~');
            else if (owner[r] == 0) putchar('.');
            else                    putchar('0' + owner[r]);
        }
        putchar('\n');
    }
}

/* ---------------- tile set ---------------- */

typedef struct {
    int tiles[N];
    int pos[N];
    int count;
} TileSet;

void ts_init(TileSet *s) {
    s->count = 0;
    for (int i = 0; i < N; i++) s->pos[i] = -1;
}

void ts_add(TileSet *s, int t) {
    if (s->pos[t] != -1) return;
    s->tiles[s->count] = t;
    s->pos[t] = s->count;
    s->count++;
}

void ts_remove(TileSet *s, int t) {
    if (s->pos[t] == -1) return;
    int i = s->pos[t];
    int last = s->tiles[s->count - 1];
    s->tiles[i] = last;
    s->pos[last] = i;
    s->pos[t] = -1;
    s->count--;
}

int ts_has(TileSet *s, int t) { return s->pos[t] != -1; }

#define HEAPCAP (8*N)

typedef struct {
    float pri[HEAPCAP];
    int   tile[HEAPCAP];
    int   count;
} Heap;

void heap_init(Heap *h) { h->count = 0; }

void heap_push(Heap *h, int t, float p) {
    if (h->count >= HEAPCAP) return;
    int i = h->count++;
    h->tile[i] = t;
    h->pri[i]  = p;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->pri[parent] <= h->pri[i]) break;
        float fp = h->pri[parent];
        int ft = h->tile[parent];
        h->pri[parent] = h->pri[i];  
        h->tile[parent] = h->tile[i];
        h->pri[i] = fp;              
        h->tile[i] = ft;
        i = parent;
    }
}

int heap_pop(Heap *h) {
    if (h->count == 0) return -1;
    int best = h->tile[0];

    h->count--;
    if (h->count == 0) return best;

    h->tile[0] = h->tile[h->count];
    h->pri[0]  = h->pri[h->count];

    int i = 0;
    for (;;) {
        int l = 2*i + 1, r = 2*i + 2, small = i;
        if (l < h->count && h->pri[l] < h->pri[small]) 
            small = l;
        if (r < h->count && h->pri[r] < h->pri[small]) 
            small = r;
        if (small == i) break;
        
        float fp = h->pri[small];   
        int ft = h->tile[small];
        h->pri[small] = h->pri[i];  
        h->tile[small] = h->tile[i];
        h->pri[i] = fp;             
        h->tile[i] = ft;
        i = small;
    }
    return best;
}

/* ---------------- players & territory ---------------- */

typedef struct {
    TileSet tiles;
    TileSet border;
    float troops;
    int alive;
} Player;

Player players[MAXP];

/* Player type. Source has Human / Nation / Bot; we hardcode two (spec 13), and
   every seat is a Bot until the agent takes one in the PufferLib skeleton.
   Three separate handicaps hang off this: maxTroops/3, troop growth x0.5, and
   the human-vs-bot mag *= 0.7 attack modifier. */
unsigned char is_bot[MAXP];

/* Annexation scheduling (spec 9). last_calc is seeded with a per-seat offset so
   the cost of the cluster scan is spread across ticks rather than spiking on
   one; last_tile_change is the cheap-out that skips players whose territory has
   not moved since their last scan. */
long last_calc[MAXP];
long last_tile_change[MAXP];

void update_border(int t) {
    int p = owner[t];
    if (p == 0) return;

    int nb[4];
    int n = neighbors(t, nb);
    int is_border = 0;
    for (int i = 0; i < n; i++) {
        if (owner[nb[i]] != p) { is_border = 1; break; }
    }

    if (is_border) 
        ts_add(&players[p].border, t);
    else           
        ts_remove(&players[p].border, t);
}

void conquer(int p, int t) {
#ifdef DEBUG
    /* source throws on water/impassable; every sim caller already filters, so
       this only exists to catch a caller that stops filtering */
    if (terrain[t] == 0) {
        printf("CONQUER WATER: p%d tile %d\n", p, t);
        exit(1);
    }
#endif
    int prev = owner[t];
    if (prev == p) return;

    if (prev != 0) {
        ts_remove(&players[prev].tiles, t);
        ts_remove(&players[prev].border, t);
    }

    owner[t] = p;
    ts_add(&players[p].tiles, t);
    /* alive is a cached "owns at least one tile" (source: isAlive() is exactly
       numTilesOwned() > 0). It is kept as a field only so player_tick/bot_tick/
       annex_tick can skip dead seats without touching the TileSet. Maintained
       in BOTH directions here, which is the only place territory moves —
       clearing it on loss alone left spawned-but-not-yet-placed seats reading
       as alive, and annexation can empty a player without going through
       attack_tick at all. */
    players[p].alive = 1;

    update_border(t);
    int nb[4];
    int n = neighbors(t, nb);
    for (int i = 0; i < n; i++) 
        update_border(nb[i]);

    last_tile_change[p] = ticks;
    if (prev != 0) last_tile_change[prev] = ticks;

    if (prev != 0 && players[prev].tiles.count == 0)
        players[prev].alive = 0;
}

#define ANNEX_PERIOD 20   /* source ticksPerClusterCalc */

void players_reset(void) {
    for (int p = 0; p < MAXP; p++) {
        ts_init(&players[p].tiles);
        ts_init(&players[p].border);
        players[p].troops = 0.0f;
        players[p].alive  = 0;   /* nobody owns tiles until spawn_place runs */
        is_bot[p]         = 1;
        /* source: lastCalc = ticks + simpleHash(id) % ticksPerClusterCalc.
           Any fixed spread works; this one is deterministic per seat. */
        last_calc[p]        = (long)(p * 7 % ANNEX_PERIOD);
        last_tile_change[p] = 0;
    }
    memset(owner, 0, sizeof(owner));
}

float max_troops(int p) {
    float tiles = (float)players[p].tiles.count;
    float m = 2.0f * (powf(tiles, 0.6f) * 1000.0f + 50000.0f);
    if (is_bot[p]) m /= 3.0f;
    return m;
}

void player_tick(int p) {
    if (!players[p].alive) return;
    float max = max_troops(p);
    float troops = players[p].troops;
    /* source order: base rate, then the capacity ratio, then the bot handicap.
       The ratio goes negative when max shrinks below current troops (territory
       loss), which is load-bearing — troops decay toward the new cap. */
    float add = 10.0f + powf(troops, 0.73f) / 4.0f;
    add *= (1.0f - troops / max);
    if (is_bot[p]) add *= 0.5f;
    troops += add;
    if (troops > max) troops = max;
    players[p].troops = troops;
}

/* ---------------- attack ---------------- */

typedef struct {
    int   active;
    int   attacker;
    int   target;
    float troops;
    Heap  heap;
} Attack;

#define MAXATK 32
Attack attacks[MAXATK];

int find_free_slot(void) {
    for (int i = 0; i < MAXATK; i++)
        if (attacks[i].active == 0) return i;
    return -1;
}

void atk_push(Attack *a, int t) {
    float mag;
    if      (terrain[t] == 1) mag = 1.0f;
    else if (terrain[t] == 2) mag = 1.5f;
    else if (terrain[t] == 3) mag = 2.0f;
    else                      mag = 0.0f;

    int nb[4];
    int n = neighbors(t, nb);
    int own = 0;
    for (int i = 0; i < n; i++)
        if (owner[nb[i]] == a->attacker) own++;

    float pri = (rng_int(0, 7) + 10) * (1.0f - own*0.5f + mag/2.0f) + ticks;
    heap_push(&a->heap, t, pri);
}

void attack_start(int attacker, int target, float troops) {
    if (attacker == target) return;

    if (troops > players[attacker].troops) troops = players[attacker].troops;
    if (troops < 1.0f) return;
    players[attacker].troops -= troops;

    for (int i = 0; i < MAXATK; i++) {
        if (!attacks[i].active) continue;
        if (attacks[i].attacker != target || attacks[i].target != attacker) continue;
        if (attacks[i].troops > troops) {
            attacks[i].troops -= troops;
            return;
        }
        troops -= attacks[i].troops;
        attacks[i].active = 0;
        if (troops <= 0.0f) return;
    }

    for (int i = 0; i < MAXATK; i++) {
        if (!attacks[i].active) continue;
        if (attacks[i].attacker != attacker || attacks[i].target != target) continue;
        troops += attacks[i].troops;
        attacks[i].active = 0;
    }

    if (troops < 1.0f) return;

    int i = find_free_slot();
    if (i < 0) {
        players[attacker].troops += troops;
#ifdef DEBUG
        printf("attack_start: no free slot (a=%d t=%d troops=%.1f)\n",
               attacker, target, troops);
#endif
        return;
    }

    Attack *a = &attacks[i];

    a->active   = 1;
    a->attacker = attacker;
    a->target = target;
    a->troops = troops;

    heap_init(&a->heap);

    TileSet *b = &players[attacker].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            if (owner[nb[k]] == target && terrain[nb[k]] != 0)
                atk_push(a, nb[k]);
        }
    }
}

/* Spec 14 suggests landTiles/50 (=42 here), but spawns are 49 tiles, so 42
   would wipe a player who has lost only 7 tiles. That is not what the rule
   means: in source the threshold is 100 of ~500k tiles, i.e. 0.02% of the map,
   whereas 42/2116 is 2%. Anchor to spawn size instead — a player must lose
   roughly two thirds of its starting territory. Measured: 8 vs 42 changes the
   elimination rate by 2 points, so this is not a sensitive constant. */
#define SPAWN_TILES 49
#define WIPE_TILES  (SPAWN_TILES / 3)

void dead_defender(int attacker, int target) {
    if (target == 0) return;

    for (int pass = 0; pass < 100; pass++) {
        int progress = 0;
        TileSet *ts = &players[target].tiles;

        for (int i = ts->count - 1; i >= 0; i--) {
            int t = ts->tiles[i];
            int nb[4];
            int n = neighbors(t, nb);
            int taker = 0;
            for (int k = 0; k < n; k++) {
                int o = owner[nb[k]];
                if (o == attacker) { taker = attacker; break; }
                if (o != 0 && o != target && taker == 0) taker = o;
            }
            if (taker != 0) { conquer(taker, t); progress = 1; }
        }

        if (!progress || players[target].tiles.count == 0) break;
    }
}

void attack_tick(Attack *a) {
    float frontier = (float)a->heap.count + rng_int(0, 5);
    float budget;
    if (a->target == 0) {
        budget = frontier * 2.0f;
    } else {
        float def = players[a->target].troops;
        if (def <= 0.0f) budget = 0.5f * frontier * 3.0f;
        else budget = within((5.0f*a->troops / def) * 2.0f, 0.01f, 0.5f) * frontier * 3.0f;
    }

    while (budget > 0.0f) {
        if (a->troops < 1.0f) { a->active = 0; return; }
        int t = heap_pop(&a->heap);
        if (t < 0) {
            players[a->attacker].troops += a->troops;
            a->active = 0;
            return;
        }

        if (owner[t] != a->target) continue;
        if (terrain[t] == 0) continue;
        int nb[4];
        int n = neighbors(t, nb);
        int on_border = 0;
        for (int i = 0; i < n; i++) {
            if (owner[nb[i]] == a->attacker) { 
                on_border = 1; 
                break; 
            }
        }
        if (!on_border) continue;   /* now this continues the while */

        for (int k = 0; k < n; k++) {
            if (owner[nb[k]] == a->target && terrain[nb[k]] != 0)
                atk_push(a, nb[k]);
        }

        float mag, speed;
        if (terrain[t] == 2)      { mag = 100.0f; speed = 20.0f; }
        else if (terrain[t] == 3) { mag = 120.0f; speed = 25.0f; }
        else                      { mag = 80.0f;  speed = 16.5f; }

        float atk_loss, cost;
        if (a->target == 0) {
            /* Bot attackers pay half the terra nullius toll. Terra nullius is
               not a Bot, so the 0.7 modifier below never applies here. */
            atk_loss = is_bot[a->attacker] ? mag / 10.0f : mag / 5.0f;
            cost = within(2000.0f * (speed > 10.0f ? speed : 10.0f) / a->troops, 5.0f, 100.0f);
        } else {
            /* Human/Nation attacking a Bot takes 30% less loss. Both sides must
               be players, which is why this sits inside the vs-player branch. */
            if (!is_bot[a->attacker] && is_bot[a->target]) mag *= 0.7f;

            float def_troops = players[a->target].troops;
            int def_tiles = players[a->target].tiles.count;
            float def_loss = (def_tiles > 0) ? def_troops / def_tiles : 0.0f;
            float cur_loss = within(def_troops / a->troops, 0.6f, 2.0f) * mag * 0.8f;
            float alt_loss = 1.3f * def_loss * (mag / 100.0f);
            atk_loss = 0.6f*cur_loss + 0.4f*alt_loss;
            cost = within(def_troops / (5.0f * a->troops), 0.2f, 1.5f) * speed;
            /* source removeTroops clamps at zero; powf(negative, 0.73) in
               player_tick would be NaN otherwise */
            players[a->target].troops -= def_loss;
            if (players[a->target].troops < 0.0f) players[a->target].troops = 0.0f;
        }

        budget -= cost;
        a->troops -= atk_loss;
        conquer(a->attacker, t);

        if (a->target != 0 && players[a->target].tiles.count < WIPE_TILES)
            dead_defender(a->attacker, a->target);
    }
}

/* ---------------- annexation (spec 9) ----------------

   Source: PlayerExecution.removeClusters. Re-read against the TS on Aug 2026;
   three things differ from what openfront_env_spec.md 9 says, and the first one
   is not cosmetic:

   1. THE INSCRIPTION TEST IS THE OTHER WAY AROUND. Util.inscribed(outer, inner)
      is called as inscribed(enemyBox, clusterBox), i.e. the ENEMY neighbor
      bounding box must contain the CLUSTER's, not the reverse. Under the spec's
      reading the rule can never fire: a ring of enemy tiles enclosing a cluster
      always extends one tile past it on every side. The correct reading is what
      makes the rule mean "the enemy wraps around us."

   2. removeCluster now gates on isEnclosed(firstTile) before transferring
      anything. The surround checks only look at one border component, but the
      fill below hands over the whole territory that component sits on. A
      component wrapped around an interior hole passes the surround test while
      sitting on an otherwise wide-open empire, so without this gate a single
      enemy enclave would donate the entire player. Unclaimed land is walked
      through (a hole is not an exit); water and the map edge end it.

   3. isOceanShore (largest-cluster rule) and isShore (every other cluster)
      collapse to the same predicate here — no lake/ocean distinction and no
      shoreline bit, so both are "land tile touching water" (is_shore).

   Cost is O(border), not O(territory): the cluster fill only ever walks the
   player's border set, and last_tile_change skips players who haven't moved. */

/* Source checks numTilesOwned() < 100 to force a scan regardless of the 20-tick
   period. Same rescale argument as WIPE_TILES — 100 is 0.02% of a real map. */
#define ANNEX_TILES WIPE_TILES

static unsigned int cl_visited[N];     /* generation stamps, never cleared */
static unsigned int cl_gen = 0;
static int cl_stack[N];                /* reused DFS stack */
static int cl_comp[N];                 /* all components concatenated */
static int cl_start[N];
static int cl_size[N];

static unsigned int ff_visited[N];     /* separate array: the territory fill runs */
static unsigned int ff_gen = 0;        /* inside the component loop and must not */
static int ff_stack[N];                /* disturb cl_visited */
static int ff_take[N];

long annex_events = 0;
long annex_tiles_moved = 0;

/* largest != 0 selects surroundedBySamePlayer (every 4-neighbor owned, exactly
   one distinct enemy); otherwise isSurrounded (unowned neighbors allowed, any
   number of enemies). Both require the enemy box to contain the cluster box. */
static int annex_surrounded(int p, const int *tiles, int n, int largest) {
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

        if (is_shore(t) || on_map_edge(t)) return 0;

        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            int o = owner[u];
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
        /* source re-checks enemies.size !== 1 after every tile, so a second
           distinct enemy aborts immediately rather than at the end */
        if (largest && distinct != 1) return 0;
    }

    if (largest && distinct != 1) return 0;
    if (!largest && distinct == 0) return 0;

    return eminx <= cminx && eminy <= cminy && emaxx >= cmaxx && emaxy >= cmaxy;
}

/* Can the territory reachable from start escape to water or the map edge,
   walking our own tiles and unclaimed land but not other players'? */
static int annex_enclosed(int p, int start) {
    ff_gen++;
    if (ff_gen == 0) { memset(ff_visited, 0, sizeof(ff_visited)); ff_gen = 1; }

    int sp = 0;
    ff_visited[start] = ff_gen;
    ff_stack[sp++] = start;

    while (sp > 0) {
        int t = ff_stack[--sp];
        if (on_map_edge(t)) return 0;
        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            if (ff_visited[u] == ff_gen) continue;
            int o = owner[u];
            if (o != 0 && o != p) continue;            /* someone else's tile: wall */
            if (o == 0 && terrain[u] == 0) return 0;   /* open water: a way out */
            ff_visited[u] = ff_gen;
            ff_stack[sp++] = u;
        }
    }
    return 1;
}

/* The enemy running the largest attack on p, restricted to enemies bordering
   this cluster; failing that, the enemy bordering it on the most tiles. */
static int annex_capturer(int p, const int *tiles, int n) {
    int cnt[MAXP];
    memset(cnt, 0, sizeof(cnt));
    int any = 0;

    for (int i = 0; i < n; i++) {
        int nb[4];
        int k = neighbors(tiles[i], nb);
        for (int j = 0; j < k; j++) {
            int o = owner[nb[j]];
            if (o == 0 || o == p) continue;
            cnt[o]++;
            any = 1;
        }
    }
    if (!any) return 0;

    int best = 0;
    float best_troops = 0.0f;
    for (int i = 0; i < MAXATK; i++) {
        if (!attacks[i].active) continue;
        if (attacks[i].target != p) continue;
        if (cnt[attacks[i].attacker] == 0) continue;
        if (attacks[i].troops > best_troops) {
            best_troops = attacks[i].troops;
            best = attacks[i].attacker;
        }
    }
    if (best != 0) return best;

    int mode = 0, mode_cnt = 0;
    for (int q = 1; q < MAXP; q++)
        if (cnt[q] > mode_cnt) { mode_cnt = cnt[q]; mode = q; }
    return mode;
}

static void annex_remove(int p, const int *tiles, int n) {
    /* an earlier removal this tick may already have taken these tiles */
    for (int i = 0; i < n; i++)
        if (owner[tiles[i]] != p) return;

    int cap = annex_capturer(p, tiles, n);
    if (cap == 0) return;
    if (!annex_enclosed(p, tiles[0])) return;

    ff_gen++;
    if (ff_gen == 0) { memset(ff_visited, 0, sizeof(ff_visited)); ff_gen = 1; }

    int sp = 0, ntake = 0;
    ff_visited[tiles[0]] = ff_gen;
    ff_stack[sp++] = tiles[0];

    while (sp > 0) {
        int t = ff_stack[--sp];
        ff_take[ntake++] = t;
        int nb[4];
        int k = neighbors(t, nb);
        for (int j = 0; j < k; j++) {
            int u = nb[j];
            if (ff_visited[u] == ff_gen) continue;
            if (owner[u] != p) continue;
            ff_visited[u] = ff_gen;
            ff_stack[sp++] = u;
        }
    }

    /* collect first, conquer second — conquer rewrites owner[] under the fill */
    for (int i = 0; i < ntake; i++) conquer(cap, ff_take[i]);

    annex_events++;
    annex_tiles_moved += ntake;
}

void annex_tick(int p) {
    if (!players[p].alive) return;
    TileSet *b = &players[p].border;
    if (b->count == 0) return;

    if (!(ticks - last_calc[p] > ANNEX_PERIOD ||
          players[p].tiles.count < ANNEX_TILES)) return;
    if (last_tile_change[p] < last_calc[p]) return;
    last_calc[p] = ticks;

    cl_gen++;
    if (cl_gen == 0) { memset(cl_visited, 0, sizeof(cl_visited)); cl_gen = 1; }

    /* 8-connected components of the border set */
    int ncomp = 0, ntot = 0;
    for (int i = 0; i < b->count; i++) {
        int s = b->tiles[i];
        if (cl_visited[s] == cl_gen) continue;

        cl_start[ncomp] = ntot;
        int sp = 0;
        cl_visited[s] = cl_gen;
        cl_stack[sp++] = s;

        while (sp > 0) {
            int t = cl_stack[--sp];
            cl_comp[ntot++] = t;
            int nb[8];
            int k = neighbors8(t, nb);
            for (int j = 0; j < k; j++) {
                int u = nb[j];
                if (cl_visited[u] == cl_gen) continue;
                if (!ts_has(b, u)) continue;
                cl_visited[u] = cl_gen;
                cl_stack[sp++] = u;
            }
        }
        cl_size[ncomp] = ntot - cl_start[ncomp];
        ncomp++;
    }
    if (ncomp == 0) return;

    int largest = 0;
    for (int i = 1; i < ncomp; i++)
        if (cl_size[i] > cl_size[largest]) largest = i;

    if (annex_surrounded(p, &cl_comp[cl_start[largest]], cl_size[largest], 1))
        annex_remove(p, &cl_comp[cl_start[largest]], cl_size[largest]);

    for (int i = 0; i < ncomp; i++) {
        if (i == largest) continue;
        if (!players[p].alive) return;
        if (annex_surrounded(p, &cl_comp[cl_start[i]], cl_size[i], 0))
            annex_remove(p, &cl_comp[cl_start[i]], cl_size[i]);
    }
}

typedef struct {
    int attack_rate;      /* ticks between decisions, 40..80 */
    int attack_off;       /* phase offset, 0..attack_rate-1 */
    float trigger_ratio;  /* 0.50..0.60 */
    float reserve_ratio;  /* 0.30..0.40 */
    float expand_ratio;   /* 0.10..0.20 */
    int neighbors_tn;     /* starts 1, cleared permanently when no TN left */
} Bot;

Bot bots[MAXP];

void bots_init(void) {
    for (int i = 1; i<MAXP; i++){
        bots[i].attack_rate   = rng_int(40, 80);
        bots[i].attack_off    = rng_below(bots[i].attack_rate);
        bots[i].trigger_ratio = rng_int(50, 60) / 100.0f;
        bots[i].reserve_ratio = rng_int(30, 40) / 100.0f;
        bots[i].expand_ratio  = rng_int(10, 20) / 100.0f;
        bots[i].neighbors_tn = 1;
    }
}

int has_tn_neighbor(int p) {
    TileSet *b = &players[p].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            if (owner[nb[k]] == 0 && terrain[nb[k]] != 0)
                return 1;
        }
    }
    return 0;
}

int bordering_players(int p, int *out) {
    int count = 0;                         /* declared once, here */
    TileSet *b = &players[p].border;
    for (int j = 0; j < b->count; j++) {
        int t = b->tiles[j];
        int nb[4];
        int n = neighbors(t, nb);
        for (int k = 0; k < n; k++) {
            int o = owner[nb[k]];          /* needs the type */
            if (o != 0 && o != p) {
                int found = 0;
                for (int i = 0; i < count; i++) {
                    if (out[i] == o) { found = 1; break; }
                }
                if (!found) out[count++] = o;            
            }
        }
    }
    return count;                          /* not out */
}

/* Largest active attack aimed at p, returning its attacker (0 if none).
   Non-bots ignore bot attackers (findIncomingAttackPlayer); bots count
   everyone. Only bots call this today, but the agent seat will. */
int largest_incoming_attacker(int p) {
    int best = 0;
    float best_troops = 0.0f;
    for (int i = 0; i < MAXATK; i++) {
        if (!attacks[i].active) continue;
        if (attacks[i].target != p) continue;
        if (!is_bot[p] && is_bot[attacks[i].attacker]) continue;
        if (attacks[i].troops > best_troops) {
            best_troops = attacks[i].troops;
            best = attacks[i].attacker;
        }
    }
    return best;
}

/* AiAttackBehavior.attackRandomTarget. The trigger gate comes FIRST — source
   checks hasTriggerRatioTroops before looking for retaliation targets, so
   "forced" retaliation only skips shouldAttack, which is a no-op for a bot
   attacker anyway. Every branch reserves maxTroops*reserve_ratio. */
void bot_attack_random(int p) {
    if (players[p].troops < bots[p].trigger_ratio * max_troops(p)) return;

    float send = players[p].troops - max_troops(p) * bots[p].reserve_ratio;
    if (send < 1.0f) return;   /* sendAttack fails identically for every target */

    int r = largest_incoming_attacker(p);
    if (r != 0) {
        attack_start(p, r, send);
        return;
    }

    int cand[MAXP];
    int count = bordering_players(p, cand);
    if (count == 0) return;

    /* shuffleArray, then take the first candidate that goes through */
    for (int i = count - 1; i > 0; i--) {
        int j = rng_below(i + 1);
        int tmp = cand[i]; cand[i] = cand[j]; cand[j] = tmp;
    }
    for (int i = 0; i < count; i++) {
        int q = cand[i];
        /* 50% skip per Human/Nation candidate; bots are always attackable */
        if (!is_bot[q] && rng_below(2) == 0) continue;
        attack_start(p, q, send);
        return;
    }
}

void bot_tick(int p) {
    if (!players[p].alive) return;
    if (ticks % bots[p].attack_rate != bots[p].attack_off) return;

    if (bots[p].neighbors_tn) {
        if (has_tn_neighbor(p)) {
            float send = players[p].troops - max_troops(p) * bots[p].expand_ratio;
            if (send >= 1.0f) {
                attack_start(p, 0, send);
                return;
            }
            /* too poor to expand: source falls through to attackRandomTarget
               rather than burning the decision tick */
        } else {
            bots[p].neighbors_tn = 0;   /* cleared permanently, then fall through */
        }
    }
    bot_attack_random(p);
}

int win_check(void) {
    int best = 0;
    int best_p = 0;
    for (int p = 1; p < MAXP; p++) {
        if (players[p].tiles.count > best) {
            best = players[p].tiles.count;
            best_p = p;
        }
    }
    if ((float)best / land_tiles > 0.8f) return best_p;
    return 0;
}

/* ---------------- spawn placement (spec 12) ---------------- */

#define SPAWN_RADIUS   4     /* Euclidean disk -> 49 tiles, matching source's ~49 */
#define SPAWN_MIN_DIST 13    /* Manhattan between centers; scaled to 48x48 */
#define SPAWN_TRIES    1000
#define SPAWN_RELAX    750   /* min-distance constraint dropped after this many */

int spawn_center[MAXP];

/* Every tile of the disk must be in bounds, land, and unowned. */
int spawn_disk_ok(int cx, int cy) {
    for (int dy = -SPAWN_RADIUS; dy <= SPAWN_RADIUS; dy++) {
        for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; dx++) {
            if (dx*dx + dy*dy > SPAWN_RADIUS*SPAWN_RADIUS) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= W || y < 0 || y >= H) return 0;
            int t = ref(x, y);
            if (terrain[t] == 0) return 0;
            if (owner[t]  != 0) return 0;
        }
    }
    return 1;
}

/* Returns 1 on success. Conquers the disk for p and records the center. */
int spawn_place(int p) {
    for (int attempt = 0; attempt < SPAWN_TRIES; attempt++) {
        int cx = rng_below(W), cy = rng_below(H);
        int c  = ref(cx, cy);

        if (terrain[c] == 0 || owner[c] != 0) continue;

        /* source rejects border tiles; for an unowned tile that means any
           4-neighbor already belongs to someone */
        int nb[4];
        int n = neighbors(c, nb), touching = 0;
        for (int k = 0; k < n; k++) if (owner[nb[k]] != 0) { touching = 1; break; }
        if (touching) continue;

        if (attempt < SPAWN_RELAX) {
            int too_close = 0;
            for (int q = 1; q < MAXP; q++) {
                if (q == p || spawn_center[q] < 0) continue;
                int dx = rx(spawn_center[q]) - cx;
                int dy = ry(spawn_center[q]) - cy;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx + dy < SPAWN_MIN_DIST) { too_close = 1; break; }
            }
            if (too_close) continue;
        }

        if (!spawn_disk_ok(cx, cy)) continue;

        for (int dy = -SPAWN_RADIUS; dy <= SPAWN_RADIUS; dy++)
            for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; dx++) {
                if (dx*dx + dy*dy > SPAWN_RADIUS*SPAWN_RADIUS) continue;
                conquer(p, ref(cx + dx, cy + dy));
            }
        spawn_center[p] = c;
        return 1;
    }
    return 0;
}

long spawn_failures = 0;

void sim_reset(void){
    /* before spawn_place, not after: conquer() now stamps last_tile_change with
       the tick counter, and a leftover value from the previous episode would
       make every player look freshly-changed on tick 0 */
    ticks = 0;
    fill_terrain();                 /* fresh map per episode */
    players_reset();
    bots_init();
    for (int i = 0; i < MAXATK; i++) attacks[i].active = 0;
    for (int p = 0; p < MAXP; p++) spawn_center[p] = -1;

    for (int p = 1; p < MAXP; p++) {
        if (!spawn_place(p)) {      /* no room left: player sits out this episode */
            players[p].alive = 0;   /* redundant since players_reset, kept explicit */
            spawn_failures++;
            continue;
        }
        players[p].troops = 1000.0f;
    }
}

/* ---- bordering-player instrumentation (DEBUG only) ---------------------- */
#ifdef DEBUG

static long nbr_hist[MAXP];       /* all samples, indexed by distinct-neighbor count */
static long nbr_hist_late[MAXP];  /* samples taken once the map is mostly claimed */
static long nbr_samples, nbr_samples_late;

/* Distinct players (excluding p and terra nullius) adjacent to p's border. */
static int count_bordering(int p) {
    int seen[MAXP];
    memset(seen, 0, sizeof(seen));
    int n = 0;
    TileSet *b = &players[p].border;
    for (int i = 0; i < b->count; i++) {
        int nb[4];
        int k = neighbors(b->tiles[i], nb);
        for (int j = 0; j < k; j++) {
            int o = owner[nb[j]];
            if (o != 0 && o != p && !seen[o]) { seen[o] = 1; n++; }
        }
    }
    return n;
}

/* Call every SAMPLE_EVERY ticks from the main loop. */
static void sample_bordering(void) {
    int claimed = 0;
    for (int p = 1; p < MAXP; p++) claimed += players[p].tiles.count;
    int late = (land_tiles > 0) && (claimed * 100 >= land_tiles * 80);

    for (int p = 1; p < MAXP; p++) {
        if (!players[p].alive) continue;
        int n = count_bordering(p);
        nbr_hist[n]++;
        nbr_samples++;
        if (late) { nbr_hist_late[n]++; nbr_samples_late++; }
    }
}

static void print_one_hist(const char *label, long *hist, long total) {
    printf("\n%s  (n=%ld)\n", label, total);
    if (total == 0) { printf("  no samples\n"); return; }
    long cum = 0;
    double mean = 0.0;
    for (int i = 0; i < MAXP; i++) mean += (double)i * hist[i];
    mean /= (double)total;
    for (int i = 0; i < MAXP; i++) {
        if (hist[i] == 0 && i > 0 && cum == total) break;
        cum += hist[i];
        double pct = 100.0 * hist[i] / total;
        double cpct = 100.0 * cum / total;
        printf("  %d neighbors: %8ld  %5.1f%%   cum %5.1f%%  ", i, hist[i], pct, cpct);
        int bars = (int)(pct / 2.0);
        for (int b = 0; b < bars; b++) putchar('#');
        putchar('\n');
    }
    printf("  mean %.2f\n", mean);
}

static void print_nbr_hist(void) {
    print_one_hist("bordering players, all ticks", nbr_hist, nbr_samples);
    print_one_hist("bordering players, map >=80% claimed", nbr_hist_late, nbr_samples_late);
}

#else
#define sample_bordering() ((void)0)
#define print_nbr_hist()   ((void)0)
#endif
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* ---------------- invariant checks (debug builds only) ---------------- */

#ifdef DEBUG
void ts_check(TileSet *s) {
    for (int i = 0; i < s->count; i++) {
        int t = s->tiles[i];
        if (s->pos[t] != i) {
            printf("BROKEN: tiles[%d]=%d but pos[%d]=%d\n", i, t, t, s->pos[t]);
            exit(1);
        }
    }
}

void check_borders(void) {
    for (int t = 0; t < N; t++) {
        int p = owner[t];
        if (p == 0) continue;
        int nb[4], n = neighbors(t, nb), should = 0;
        for (int i = 0; i < n; i++) if (owner[nb[i]] != p) should = 1;
        if (ts_has(&players[p].border, t) != should) {
            printf("BORDER BROKEN: tile %d owner %d should=%d has=%d\n",
                   t, p, should, ts_has(&players[p].border, t));
            exit(1);
        }
    }
    for (int p = 1; p < MAXP; p++) {
        for (int i = 0; i < players[p].border.count; i++) {
            int t = players[p].border.tiles[i];
            if (owner[t] != p) {
                printf("PHANTOM: p%d border has tile %d owned by %d\n", p, t, owner[t]);
                exit(1);
            }
        }
        /* alive must track tile ownership exactly: annexation can empty a
           player through a path that does not go via attack_tick */
        if ((players[p].tiles.count == 0) != (players[p].alive == 0)) {
            printf("ALIVE BROKEN: p%d tiles=%d alive=%d\n",
                   p, players[p].tiles.count, players[p].alive);
            exit(1);
        }
        if (players[p].troops < 0.0f) {
            printf("TROOPS BROKEN: p%d troops=%f\n", p, (double)players[p].troops);
            exit(1);
        }
        if (players[p].troops != players[p].troops) {
            printf("TROOPS NaN: p%d\n", p);
            exit(1);
        }
    }
}
#else
#define ts_check(s)     ((void)0)
#define check_borders() ((void)0)
#endif

void sim_run(int nticks){
    for (int tick=0; tick<nticks; tick++){
        ticks++;
        for (int p = 1; p < MAXP; p++) bot_tick(p);
        for (int i = 0; i < MAXATK; i++)
            if (attacks[i].active) attack_tick(&attacks[i]);
        /* spec 2: per player, troop growth then the annexation check */
        for (int p = 1; p < MAXP; p++) { player_tick(p); annex_tick(p); }
        if (ticks % 50 == 0) check_borders();
        if (ticks % 10 == 0) {
            sample_bordering();
            if (win_check()) return;
        }
    }
}


/* ---------------- tests (debug builds only) ---------------- */

#ifdef DEBUG
static TileSet test_set;
static int test_present[N];

void ts_test(void) {
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

void conquer_test(void) {
    memset(terrain, 1, sizeof(terrain));   /* guard in conquer rejects water */
    players_reset();
    for (int step = 0; step < 50000; step++) {
        int p = 1 + rand() % (MAXP - 1);
        int t = rand() % N;
        conquer(p, t);
        if (step % 500 == 0) check_borders();
    }
    check_borders();

    int total = 0;
    for (int p = 1; p < MAXP; p++) total += players[p].tiles.count;
    printf("conquer ok: %d tiles owned, p1 has %d tiles / %d border\n",
           total, players[1].tiles.count, players[1].border.count);
}

void blob_test(void) {
    memset(terrain, 1, sizeof(terrain));
    players_reset();

    /* solid 12x12 rectangles so interior (non-border) tiles actually exist */
    for (int p = 1; p <= 4; p++) {
        int ox = (p % 2) * 20 + 4, oy = (p / 3) * 20 + 4;
        for (int y = oy; y < oy + 12; y++)
            for (int x = ox; x < ox + 12; x++)
                conquer(p, ref(x, y));
    }
    check_borders();
    printf("blob ok: p1 tiles=%d border=%d (expect 144 / 44)\n",
           players[1].tiles.count, players[1].border.count);

    /* chew a 3x3 hole in the middle of p1 and re-verify */
    for (int y = 8; y < 11; y++)
        for (int x = 28; x < 31; x++)
            conquer(5, ref(x, y));
    check_borders();
    printf("hole ok: p1 tiles=%d border=%d (expect 135 / 56)\n",
           players[1].tiles.count, players[1].border.count);
}

static Heap test_heap;

void heap_test(void) {
    heap_init(&test_heap);

    for (int i = 0; i < N; i++)
        heap_push(&test_heap, i, (float)(rand() % 100000));

    float prev = -1.0f;
    int popped = 0;
    while (test_heap.count > 0) {
        float p = test_heap.pri[0];
        int t = heap_pop(&test_heap);
        if (t < 0) { printf("HEAP BROKEN: pop returned -1 with count>0\n"); exit(1); }
        if (p < prev) { printf("HEAP BROKEN: %f came out after %f\n", p, prev); exit(1); }
        prev = p;
        popped++;
    }
    if (popped != N) { printf("HEAP BROKEN: popped %d of %d\n", popped, N); exit(1); }

    heap_init(&test_heap);
    for (int step = 0; step < 100000; step++) {
        if (test_heap.count == 0 || rand() % 2)
            heap_push(&test_heap, rand() % N, (float)(rand() % 1000));
        else
            heap_pop(&test_heap);
        for (int i = 0; i < test_heap.count; i++) {
            int l = 2*i+1, r = 2*i+2;
            if (l < test_heap.count && test_heap.pri[i] > test_heap.pri[l]) {
                printf("HEAP BROKEN: node %d > left child\n", i); exit(1);
            }
            if (r < test_heap.count && test_heap.pri[i] > test_heap.pri[r]) {
                printf("HEAP BROKEN: node %d > right child\n", i); exit(1);
            }
        }
    }
    printf("heap ok\n");
}

void attack_test(void) {
    /* flat map so the spread pattern isn't confounded by terrain */
    memset(terrain, 1, sizeof(terrain));
    players_reset();
    bots_init();
    for (int i = 0; i < MAXATK; i++) attacks[i].active = 0;

    for (int p = 1; p < MAXP; p++) {
        int ox = 4 + ((p-1) % 4) * 11;
        int oy = 6 + ((p-1) / 4) * 20;
        for (int y = oy; y < oy + 5; y++)
            for (int x = ox; x < ox + 5; x++) conquer(p, ref(x, y));
        players[p].troops = 1000.0f;
    }
    check_borders();
    ticks = 0;

    for (int tick = 0; tick < 300; tick++) {
        ticks = tick;
        for (int p = 1; p < MAXP; p++) bot_tick(p);
        for (int i = 0; i < MAXATK; i++)
            if (attacks[i].active) attack_tick(&attacks[i]);
        for (int p = 1; p < MAXP; p++) { player_tick(p); annex_tick(p); }
        check_borders();
        if (tick % 25 == 0) {
            printf("tick %3ld:", ticks);
            for (int p = 1; p < MAXP; p++) printf(" p%d=%d", p, players[p].tiles.count);
            putchar('\n');
        }
    }
    printf("final:");
    for (int p = 1; p < MAXP; p++) printf(" p%d=%d", p, players[p].tiles.count);
    putchar('\n');
}

static void fill_rect(int p, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            conquer(p, ref(x, y));
}

/* Forces the schedule so annex_tick actually runs this call. */
static void annex_force(int p) {
    last_calc[p] = 0;
    last_tile_change[p] = ticks;
    annex_tick(p);
}

void annex_test(void) {
    memset(terrain, 1, sizeof(terrain));   /* all land: isolate the geometry */
    for (int i = 0; i < MAXATK; i++) attacks[i].active = 0;

    /* --- positive: p2 is a 3x3 enclave inside p1's 12x12 --- */
    players_reset();
    ticks = 100;
    fill_rect(1, 10, 10, 12, 12);
    fill_rect(2, 15, 15,  3,  3);
    check_borders();
    if (players[1].tiles.count != 135 || players[2].tiles.count != 9) {
        printf("ANNEX SETUP BROKEN: p1=%d p2=%d (expect 135/9)\n",
               players[1].tiles.count, players[2].tiles.count);
        exit(1);
    }

    long before = annex_events;
    annex_force(2);
    check_borders();
    if (annex_events != before + 1) {
        printf("ANNEX BROKEN: enclave not annexed (events %ld -> %ld)\n",
               before, annex_events);
        exit(1);
    }
    if (players[2].tiles.count != 0 || players[2].alive != 0) {
        printf("ANNEX BROKEN: p2 tiles=%d alive=%d (expect 0/0)\n",
               players[2].tiles.count, players[2].alive);
        exit(1);
    }
    if (players[1].tiles.count != 144) {
        printf("ANNEX BROKEN: p1 tiles=%d (expect 144)\n", players[1].tiles.count);
        exit(1);
    }

    /* p1's own outer ring must survive the same pass: it is not surrounded */
    annex_force(1);
    check_borders();
    if (players[1].tiles.count != 144) {
        printf("ANNEX BROKEN: p1 annexed itself away, tiles=%d\n",
               players[1].tiles.count);
        exit(1);
    }

    /* --- negative: two blobs sharing one front, neither is enclosed --- */
    players_reset();
    ticks = 100;
    fill_rect(1, 10, 10, 12, 12);
    fill_rect(2, 22, 10, 12, 12);
    check_borders();
    before = annex_events;
    annex_force(1);
    annex_force(2);
    check_borders();
    if (annex_events != before) {
        printf("ANNEX BROKEN: shared front annexed (events %ld -> %ld)\n",
               before, annex_events);
        exit(1);
    }
    if (players[1].tiles.count != 144 || players[2].tiles.count != 144) {
        printf("ANNEX BROKEN: shared front moved tiles p1=%d p2=%d\n",
               players[1].tiles.count, players[2].tiles.count);
        exit(1);
    }

    printf("annex ok\n");
}

void run_tests(void) {
    ts_test();
    conquer_test();
    blob_test();
    heap_test();
    annex_test();
    attack_test();
}
#else
void run_tests(void) {}
#endif

/* ---------------- main ---------------- */

void hist_run(int episodes, int nticks){
    long wins = 0, total_len = 0, survivors = 0, elim = 0;
    for (int e = 0; e < episodes; e++) {
        sim_reset();
        sim_run(nticks);
        if (ticks < nticks) wins++;
        total_len += ticks;
        for (int p = 1; p < MAXP; p++) {
            if (players[p].tiles.count > 0) survivors++;
            else elim++;
        }
    }
    printf("episodes %d: wins %ld (%.1f%%), mean length %.0f ticks, "
           "eliminated %.1f%%\n",
           episodes, wins, 100.0*wins/episodes, (double)total_len/episodes,
           100.0*elim/(elim+survivors));
    printf("annexations %ld (%.2f/episode), tiles moved %ld (%.1f/event)\n",
           annex_events, (double)annex_events/episodes, annex_tiles_moved,
           annex_events ? (double)annex_tiles_moved/annex_events : 0.0);
    print_nbr_hist();
}

void bench(void) {
    long total_ticks = 0;
    int episodes = 0;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (total_ticks < 2000000) {
        sim_reset();
        sim_run(1000);
        total_ticks += 1000;
        episodes++;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%ld ticks in %d episodes, %.3f s -> %.0f ticks/sec\n",
           total_ticks, episodes, secs, total_ticks / secs);
}

int main(void) {
    rng_seed(42);
    run_tests();
    hist_run(300, 2000);
    return 0;
}