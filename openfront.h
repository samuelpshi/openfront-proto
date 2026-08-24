/* Territory conquest sim. Mechanics derived from OpenFrontIO
   (github.com/openfrontio/OpenFrontIO, AGPL-3.0), commit fc50009.
   Independent C implementation; no source transliterated. */

#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef float obs_t;
#include "pufferenv.h"

#define ACT_NEIGHBORS 5
#define ACT_SIZES {7}
#define NUM_ATNS  1
#define OBS_SIZE  31
_Static_assert(OBS_SIZE == 6 + 5*ACT_NEIGHBORS, "OBS_SIZE out of sync");

#define W 48
#define H 48
#define N (W*H)
#define MAXP 9
#define MAXATK 32

#define HEAPCAP 2048

#define ANNEX_PERIOD 20
#define SPAWN_TILES  49
#define WIPE_TILES   (SPAWN_TILES/3)
#define ANNEX_TILES  WIPE_TILES

#define SPAWN_RADIUS   4
#define SPAWN_MIN_DIST 13
#define SPAWN_TRIES    1000
#define SPAWN_RELAX    750

#define START_TROOPS_HUMAN 25000.0f
#define START_TROOPS_BOT   10000.0f

// types

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
    int attack_rate;
    int attack_off;
    float trigger_ratio;
    float reserve_ratio;
    float expand_ratio;
    int neighbors_tn;
} Bot;

typedef struct Log Log;
struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float win;
    float annexations;
    float n;
};

typedef struct {
    int   seat;
    int   done;
    int   prev_tiles;
    int   decisions;
    float episode_return;
} Seat;

typedef struct Env Env;
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[MAXP-1];
    int tag, boundary_reached;

    Seat seats[MAXP-1];
    int action_repeat;
    int max_steps;
    int steps;
    int agent_is_bot;

    unsigned char  terrain[N];
    unsigned short owner[N];
    int  land_tiles;
    long ticks;

    Player players[MAXP];

    unsigned char is_bot[MAXP];

    long last_calc[MAXP];
    long last_tile_change[MAXP];

    Attack attacks[MAXATK];
    Bot    bots[MAXP];
    int    spawn_center[MAXP];

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

    long annex_events;
    long annex_by[MAXP];
    long annex_tiles_moved;
    long spawn_failures;
    long heap_full_drops;
    int  heap_peak;
};

// helpers

static int ref(int x, int y) { return y*W + x; }
static int rx(int r) { return r % W; }
static int ry(int r) { return r / W; }

static float within(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int neighbors(int r, int *out) {
    int n = 0;
    if (ry(r) != 0)   out[n++] = r-W;
    if (ry(r) != H-1) out[n++] = r+W;
    if (rx(r) != 0)   out[n++] = r-1;
    if (rx(r) != W-1) out[n++] = r+1;
    return n;
}

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

// rng

static void rng_seed(Env *e, unsigned int s) {
    s ^= s >> 16; s *= 0x7feb352du;
    s ^= s >> 15; s *= 0x846ca68bu;
    s ^= s >> 16;
    e->rng = (s == 0) ? 1u : s;
}

static unsigned int rng_next(Env *e) {
    unsigned int x = e->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng = x;
    return x;
}

static int rng_below(Env *e, int n) {
    return (int)(rng_next(e) % (unsigned int)n);
}

static int rng_int(Env *e, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + rng_below(e, hi - lo);
}

// map

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

// tileset

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

// heap

static void heap_init(Heap *h) { h->count = 0; }

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

// territory

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

static void conquer(Env *e, int p, int t) {
#ifdef DEBUG
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
        e->last_calc[p]        = (long)(p * 7 % ANNEX_PERIOD);
        e->last_tile_change[p] = 0;
    }
    memset(e->owner, 0, sizeof(e->owner));
}

// economy

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
    float add = 10.0f + powf(troops, 0.73f) / 4.0f;
    add *= (1.0f - troops / max);
    if (e->is_bot[p]) add *= 0.5f;
    troops += add;
    if (troops > max) troops = max;
    e->players[p].troops = troops;
}

// attack

static int find_free_slot(Env *e) {
    for (int i = 0; i < MAXATK; i++)
        if (e->attacks[i].active == 0) return i;
    return -1;
}

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

static void attack_start(Env *e, int attacker, int target, float troops) {
    if (attacker == target) return;

    if (troops > e->players[attacker].troops) troops = e->players[attacker].troops;
    if (troops < 1.0f) return;
    e->players[attacker].troops -= troops;

    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].attacker != target || e->attacks[i].target != attacker) continue;
        if (e->attacks[i].troops > troops) {
            e->attacks[i].troops -= troops;
            return;
        }
        troops -= e->attacks[i].troops;
        e->attacks[i].active = 0;
        if (troops <= 0.0f) return;
    }

    for (int i = 0; i < MAXATK; i++) {
        if (!e->attacks[i].active) continue;
        if (e->attacks[i].attacker != attacker || e->attacks[i].target != target) continue;
        troops += e->attacks[i].troops;
        e->attacks[i].active = 0;
    }

    if (troops < 1.0f) return;

    int i = find_free_slot(e);
    if (i < 0) {
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
    float frontier = (float)a->heap.count + rng_int(e, 0, 5);
    float budget;
    if (a->target == 0) {
        budget = frontier * 2.0f;
    } else {
        float def = e->players[a->target].troops;
        if (def <= 0.0f) budget = 0.5f * frontier * 3.0f;
        else budget = within((5.0f*a->troops / def) * 2.0f, 0.01f, 0.5f) * frontier * 3.0f;
    }

    while (budget > 0.0f) {
        if (a->troops < 1.0f) { a->active = 0; return; }

        int t = heap_pop(&a->heap);
        if (t < 0) {
            e->players[a->attacker].troops += a->troops;
            a->active = 0;
            return;
        }

        if (e->owner[t] != a->target) continue;
        if (e->terrain[t] == 0) continue;
        int nb[4];
        int n = neighbors(t, nb);
        int on_border = 0;
        for (int i = 0; i < n; i++) {
            if (e->owner[nb[i]] == a->attacker) { on_border = 1; break; }
        }
        if (!on_border) continue;

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
            atk_loss = e->is_bot[a->attacker] ? mag / 10.0f : mag / 5.0f;
            cost = within(2000.0f * (speed > 10.0f ? speed : 10.0f) / a->troops, 5.0f, 100.0f);
        } else {
            if (!e->is_bot[a->attacker] && e->is_bot[a->target]) mag *= 0.7f;

            float def_troops = e->players[a->target].troops;
            int   def_tiles  = e->players[a->target].tiles.count;
            float def_loss = (def_tiles > 0) ? def_troops / def_tiles : 0.0f;
            float cur_loss = within(def_troops / a->troops, 0.6f, 2.0f) * mag * 0.8f;
            float alt_loss = 1.3f * def_loss * (mag / 100.0f);
            atk_loss = 0.6f*cur_loss + 0.4f*alt_loss;
            cost = within(def_troops / (5.0f * a->troops), 0.2f, 1.5f) * speed;
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

// annexation

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
                if (largest) return 0;
                continue;
            }
            if (!seen[o]) { seen[o] = 1; distinct++; }
            int ux = rx(u), uy = ry(u);
            if (ux < eminx) eminx = ux;
            if (ux > emaxx) emaxx = ux;
            if (uy < eminy) eminy = uy;
            if (uy > emaxy) emaxy = uy;
        }
        if (largest && distinct != 1) return 0;
    }

    if (largest && distinct != 1) return 0;
    if (!largest && distinct == 0) return 0;

    return eminx <= cminx && eminy <= cminy && emaxx >= cmaxx && emaxy >= cmaxy;
}

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
            if (o != 0 && o != p) continue;
            if (o == 0 && e->terrain[u] == 0) return 0;
            e->ff_visited[u] = e->ff_gen;
            e->ff_stack[sp++] = u;
        }
    }
    return 1;
}

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
            int dup = 0;
            for (int m = 0; m < j; m++)
                if (e->owner[nb[m]] == o) { dup = 1; break; }
            if (dup) continue;
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

    for (int i = 0; i < ntake; i++) conquer(e, cap, e->ff_take[i]);

    e->annex_events++;
    e->annex_by[cap]++;
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

// bots

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

static void bot_attack_random(Env *e, int p) {
    if (e->players[p].troops < e->bots[p].trigger_ratio * max_troops(e, p)) return;

    float send = e->players[p].troops - max_troops(e, p) * e->bots[p].reserve_ratio;
    if (send < 1.0f) return;

    int r = largest_incoming_attacker(e, p);
    if (r != 0) {
        attack_start(e, p, r, send);
        return;
    }

    int cand[MAXP];
    int count = bordering_players(e, p, cand);
    if (count == 0) return;

    for (int i = count - 1; i > 0; i--) {
        int j = rng_below(e, i + 1);
        int tmp = cand[i]; cand[i] = cand[j]; cand[j] = tmp;
    }
    for (int i = 0; i < count; i++) {
        int q = cand[i];
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
        } else {
            e->bots[p].neighbors_tn = 0;   /* cleared permanently, then fall through */
        }
    }
    bot_attack_random(e, p);
}

// win condition

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

// spawn placement

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

static int spawn_place(Env *e, int p) {
    for (int attempt = 0; attempt < SPAWN_TRIES; attempt++) {
        int cx = rng_below(e, W), cy = rng_below(e, H);
        int c  = ref(cx, cy);

        if (e->terrain[c] == 0 || e->owner[c] != 0) continue;

        int nb[4];
        int n = neighbors(c, nb), touching = 0;
        for (int k = 0; k < n; k++) if (e->owner[nb[k]] != 0) { touching = 1; break; }
        if (touching) continue;

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

// episode lifecycle

static void sim_reset(Env *e) {
    e->ticks = 0;
    fill_terrain(e);
    players_reset(e);
    bots_init(e);
    for (int i = 0; i < MAXATK; i++) e->attacks[i].active = 0;
    for (int p = 0; p < MAXP; p++) e->spawn_center[p] = -1;

    for (int p = 1; p < MAXP; p++) {
        if (!spawn_place(e, p)) {
            e->players[p].alive = 0;
            e->spawn_failures++;
            continue;
        }
        e->players[p].troops = start_troops(e, p);
    }
}

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

static int sim_tick(Env *e) {
    e->ticks++;
    for (int p = 1; p < MAXP; p++) {
        if (p <= e->num_agents) continue;
        bot_tick(e, p);
    }
    for (int i = 0; i < MAXATK; i++)
        if (e->attacks[i].active) attack_tick(e, &e->attacks[i]);
    for (int p = 1; p < MAXP; p++) { player_tick(e, p); annex_tick(e, p); }
    if (e->ticks % 50 == 0) check_borders(e);
    if (e->ticks % 10 == 0) return win_check(e);
    return 0;
}

static void sim_run(Env *e, int nticks) {
    for (int tick = 0; tick < nticks; tick++)
        if (sim_tick(e)) return;
}

// invariant checks (debug builds only)

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

// tests (debug builds only)

#ifdef DEBUG

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
    memset(e->terrain, 1, sizeof(e->terrain));
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

static void annex_force(Env *e, int p) {
    e->last_calc[p] = 0;
    e->last_tile_change[p] = e->ticks;
    annex_tick(e, p);
}

static void annex_test(void) {
    Env *e = test_env(4);
    memset(e->terrain, 1, sizeof(e->terrain));

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

    annex_force(e, 1);
    check_borders(e);
    if (e->players[1].tiles.count != 144) {
        printf("ANNEX BROKEN: p1 annexed itself away, tiles=%d\n",
               e->players[1].tiles.count);
        exit(1);
    }

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
    memset(e->terrain, 1, sizeof(e->terrain));
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

    for (int i = 0; i < ISO_ENVS; i++) {
        Env *e = test_env(seeds[i]);
        for (int ep = 0; ep < ISO_EPS; ep++) {
            sim_reset(e);
            sim_run(e, ISO_TICKS);
        }
        alone[i] = env_hash(e);
        free(e);
    }

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

// observations

static int sorted_neighbors(Env *e, int p, int *out, int *shared) {
    int cnt[MAXP];
    memset(cnt, 0, sizeof(cnt));

    TileSet *b = &e->players[p].border;
    for (int i = 0; i < b->count; i++) {
        int nb[4];
        int n = neighbors(b->tiles[i], nb);
        for (int k = 0; k < n; k++) {
            int o = e->owner[nb[k]];
            if (o != 0 && o != p) cnt[o]++;
        }
    }

    int n_out = 0;
    for (int q = 1; q < MAXP; q++) {
        if (cnt[q] == 0) continue;
        int pos = n_out;
        while (pos > 0 && cnt[q] > shared[pos-1]) {
            out[pos]    = out[pos-1];
            shared[pos] = shared[pos-1];
            pos--;
        }
        out[pos]    = q;
        shared[pos] = cnt[q];
        n_out++;
    }
    return n_out > ACT_NEIGHBORS ? ACT_NEIGHBORS : n_out;
}

static void compute_observations(Env *e) {
    for (int a = 0; a < e->num_agents; a++) {
        obs_t *obs = e->agents[a].observations;
        memset(obs, 0, OBS_SIZE * sizeof(obs_t));
        if (e->seats[a].done) continue;

        int p = e->seats[a].seat;
        int idx = 0;
        float troops = e->players[p].troops;
        float maxt   = max_troops(e, p);
        int   tiles  = e->players[p].tiles.count;
        int   border = e->players[p].border.count;

        obs[idx++] = within(troops / maxt, 0.0f, 1.0f);
        obs[idx++] = (float)tiles / e->land_tiles;
        obs[idx++] = tiles ? (float)border / tiles : 0.0f;
        obs[idx++] = has_tn_neighbor(e, p) ? 1.0f : 0.0f;

        int nb_p[MAXP], nb_shared[MAXP];
        int n_nb = sorted_neighbors(e, p, nb_p, nb_shared);
        int shared_total = 0;
        for (int k = 0; k < n_nb; k++) shared_total += nb_shared[k];
        obs[idx++] = (float)n_nb / ACT_NEIGHBORS;
        obs[idx++] = (float)e->steps / e->max_steps;

        for (int k = 0; k < ACT_NEIGHBORS; k++) {
            if (k >= n_nb) { idx += 5; continue; }
            int q = nb_p[k];
            obs[idx++] = 1.0f;
            obs[idx++] = shared_total ? (float)nb_shared[k] / shared_total : 0.0f;
            obs[idx++] = (float)e->players[q].tiles.count / e->land_tiles;
            float mine = troops > 1.0f ? troops : 1.0f;
            float thrs = e->players[q].troops > 1.0f ? e->players[q].troops : 1.0f;
            obs[idx++] = within(log2f(thrs / mine), -4.0f, 4.0f) / 8.0f + 0.5f;
            obs[idx++] = 0.0f;
        }

        for (int i = 0; i < MAXATK; i++) {
            if (!e->attacks[i].active || e->attacks[i].target != p) continue;
            for (int k = 0; k < n_nb; k++) {
                if (nb_p[k] == e->attacks[i].attacker) {
                    obs[6 + 5*k + 4] = 1.0f;
                    break;
                }
            }
        }
    }
}

// actions

static void apply_action(Env *e, int seat_idx) {
    Seat *s = &e->seats[seat_idx];
    if (s->done) return;

    int p = s->seat;
    if (!e->players[p].alive) return;

    int action = (int)e->agents[seat_idx].actions[0];
    if (action <= 0) return;

    float send = e->players[p].troops / 5.0f;
    if (send < 1.0f) return;

    if (action == 1) {
        if (has_tn_neighbor(e, p)) attack_start(e, p, 0, send);
        return;
    }

    int nb_p[MAXP], nb_shared[MAXP];
    int n_nb = sorted_neighbors(e, p, nb_p, nb_shared);
    int k = action - 2;
    if (k < n_nb) attack_start(e, p, nb_p[k], send);
}

// puffer lifecycle

static void add_log(Env *e, int seat_idx) {
    if (seat_idx != 0) return;
    Seat *s = &e->seats[seat_idx];
    int p = s->seat;
    e->log.perf           += (float)e->players[p].tiles.count / e->land_tiles;
    e->log.score          += (float)e->players[p].tiles.count / e->land_tiles;
    e->log.episode_return += s->episode_return;
    e->log.episode_length += s->decisions;
    e->log.win            += ((float)e->players[p].tiles.count / e->land_tiles > 0.8f);
    e->log.annexations    += e->annex_by[p];
    e->log.n              += 1.0f;
}

void puf_reset(Env *e) {
    sim_reset(e);
    for (int a = 0; a < e->num_agents; a++) {
        int p = a + 1;
        e->is_bot[p] = (unsigned char)e->agent_is_bot;
        if (e->players[p].alive)
            e->players[p].troops = start_troops(e, p);
        e->seats[a].seat            = p;
        e->seats[a].done            = e->players[p].alive ? 0 : 1;
        e->seats[a].prev_tiles      = e->players[p].tiles.count;
        e->seats[a].decisions       = 0;
        e->seats[a].episode_return  = 0.0f;
    }
    e->steps = 0;
    e->annex_events = 0;
    memset(e->annex_by, 0, sizeof(e->annex_by));
    compute_observations(e);
}

void puf_step(Env *e) {
    for (int a = 0; a < e->num_agents; a++) {
        e->agents[a].rewards[0]   = 0.0f;
        e->agents[a].terminals[0] = 0.0f;
    }

    for (int a = 0; a < e->num_agents; a++) apply_action(e, a);

    int winner = 0;
    for (int r = 0; r < e->action_repeat; r++) {
        winner = sim_tick(e);
        if (winner) break;
    }
    e->steps++;

    int episode_over = (winner != 0) || (e->steps >= e->max_steps);

    for (int a = 0; a < e->num_agents; a++) {
        Seat *s = &e->seats[a];
        if (s->done) continue;
        int p = s->seat;
        int tiles = e->players[p].tiles.count;

        float reward = (float)(tiles - s->prev_tiles) / e->land_tiles;
        s->prev_tiles = tiles;
        s->decisions++;

        int died = (tiles == 0);
        if (died)                   reward -= 1.0f;
        else if (winner == p)       reward += 1.0f;

        e->agents[a].rewards[0] = reward;
        s->episode_return += reward;

        if (died || episode_over) {
            e->agents[a].terminals[0] = 1.0f;
            s->done = 1;
            add_log(e, a);
        }
    }

    if (episode_over) puf_reset(e);
    else compute_observations(e);
}

void puf_render(Env *e) {
    const int CELL = 14;
    if (!IsWindowReady()) {
        InitWindow(W*CELL, H*CELL, "OpenFront");
        SetTargetFPS(30);
    }
    if (IsKeyDown(KEY_ESCAPE)) exit(0);
    static const Color PAL[MAXP] = {
        {30, 30, 40, 255},    {228, 90, 80, 255},   {80, 160, 228, 255},
        {110, 200, 110, 255}, {228, 200, 80, 255},  {180, 110, 220, 255},
        {90, 210, 200, 255},  {230, 140, 60, 255},  {200, 200, 210, 255},
    };
    BeginDrawing();
    ClearBackground((Color){6, 24, 24, 255});
    for (int t = 0; t < N; t++) {
        Color c;
        if (e->terrain[t] == 0)      c = (Color){20, 40, 70, 255};
        else if (e->owner[t] == 0)   c = (Color){60, 70, 60, 255};
        else                         c = PAL[e->owner[t]];
        DrawRectangle(rx(t)*CELL, ry(t)*CELL, CELL, CELL, c);
    }
    EndDrawing();
}

// puffer interface

void puf_init(Env *e, Dict *kwargs) {
    e->num_agents    = (int)dict_get(kwargs, "num_agents");
    if (e->num_agents < 1)      e->num_agents = 1;
    if (e->num_agents > MAXP-1) e->num_agents = MAXP-1;
    e->action_repeat = (int)dict_get(kwargs, "action_repeat");
    e->max_steps     = (int)dict_get(kwargs, "max_steps");
    e->agent_is_bot  = (int)dict_get(kwargs, "agent_is_bot");

    rng_seed(e, e->rng);

    for (int i = 0; i < e->num_agents; i++) {
        e->agents[i].policy      = 0;
        e->agents[i].action_mask = NULL;
        e->seats[i].seat         = i + 1;
    }
    memset(&e->log, 0, sizeof(Log));
}

void puf_log(Log *log, Dict *out) {
    dict_set(out, "perf",           log->perf);
    dict_set(out, "score",          log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "win",            log->win);
    dict_set(out, "annexations",    log->annexations);
    dict_set(out, "n",              log->n);
}

void puf_close(Env *e) {
    (void)e;
}
