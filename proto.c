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

void fill_terrain(void) {
    for (int r = 0; r < N; r++) {
        if (ry(r) == 0 || ry(r) == H-1 || rx(r) == 0 || rx(r) == W-1)
            terrain[r] = 0;
        else
            terrain[r] = 1;
    }

    for (int i = 0; i < 15; i++) {
        int cx  = rand() % W;
        int cy  = rand() % H;
        int rad = 4 + rand() % 8;
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
    int prev = owner[t];
    if (prev == p) return;

    if (prev != 0) {
        ts_remove(&players[prev].tiles, t);
        ts_remove(&players[prev].border, t);
    }

    owner[t] = p;
    ts_add(&players[p].tiles, t);

    update_border(t);
    int nb[4];
    int n = neighbors(t, nb);
    for (int i = 0; i < n; i++) 
        update_border(nb[i]);
}

void players_reset(void) {
    for (int p = 0; p < MAXP; p++) {
        ts_init(&players[p].tiles);
        ts_init(&players[p].border);
        players[p].troops = 0.0f;
        players[p].alive  = 1;
    }
    memset(owner, 0, sizeof(owner));
}

float max_troops(int p) {
    float tiles = (float)players[p].tiles.count;
    return 2.0f * (powf(tiles, 0.6f) * 1000.0f + 50000.0f);
}

void player_tick(int p) {
    if (!players[p].alive) return;
    float max = max_troops(p);
    float troops = players[p].troops;
    float add = (10.0f + powf(troops, 0.73f) / 4.0f) * (1.0f - troops / max);
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

    float pri = (rand() % 8 + 10) * (1.0f - own*0.5f + mag/2.0f) + ticks;
    heap_push(&a->heap, t, pri);
}

void attack_start(int attacker, int target, float troops) {
    int i = find_free_slot();
    if (i < 0) return;                 /* all slots busy, drop the attack */

    Attack *a = &attacks[i];           /* pointer to the real slot, not a copy */

    a->active   = 1;
    a->attacker = attacker;
    a->target = target;
    a->troops = troops;

    heap_init(&a->heap);

    players[attacker].troops -= troops;

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

void attack_tick(Attack *a) {
    float frontier = (float)a->heap.count + (rand() % 6);
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
            atk_loss = mag / 5.0f;
            cost = within(2000.0f * (speed > 10.0f ? speed : 10.0f) / a->troops, 5.0f, 100.0f);
        } else {
            float def_troops = players[a->target].troops;
            int def_tiles = players[a->target].tiles.count;
            float def_loss = (def_tiles > 0) ? def_troops / def_tiles : 0.0f;
            float cur_loss = within(def_troops / a->troops, 0.6f, 2.0f) * mag * 0.8f;
            float alt_loss = 1.3f * def_loss * (mag / 100.0f);
            atk_loss = 0.6f*cur_loss + 0.4f*alt_loss;
            cost = within(def_troops / (5.0f * a->troops), 0.2f, 1.5f) * speed;
            players[a->target].troops -= def_loss;
        }

        budget -= cost;
        a->troops -= atk_loss;
        conquer(a->attacker, t);
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
        bots[i].attack_rate = 40 + rand() % 41;
        bots[i].attack_off = rand() % bots[i].attack_rate;
        bots[i].trigger_ratio = (50 + rand() % 11) / 100.0f;
        bots[i].reserve_ratio = (30 + rand() % 11) / 100.0f;
        bots[i].expand_ratio = (10 + rand() % 11) / 100.0f;
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

void bot_tick(int p) {
    if (!players[p].alive) return;
    if (ticks % bots[p].attack_rate != bots[p].attack_off) return;

    if (bots[p].neighbors_tn) {
        if (has_tn_neighbor(p) == 0) {
            bots[p].neighbors_tn = 0;
            return;
        }
        
        float send = players[p].troops - max_troops(p) * bots[p].expand_ratio;
        if (send < 1.0f) return;
        
        attack_start(p, 0, send);
    } else {
        if (players[p].troops < bots[p].trigger_ratio * max_troops(p)) return;
        int cand[MAXP];
        int count = bordering_players(p, cand);
        if (count == 0) return;
        int target = cand[rand() % count];
        float send = players[p].troops - max_troops(p) * bots[p].reserve_ratio;
        if (send < 1.0f) return;
        attack_start(p, target, send);
    }
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

void sim_reset(void){
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
    ticks = 0;
}
void sim_run(int nticks){
    for (int tick=0; tick<nticks;tick++){
        ticks++;
        for (int p = 1; p < MAXP; p++) bot_tick(p);
        for (int i = 0; i < MAXATK; i++)
            if (attacks[i].active) attack_tick(&attacks[i]);
        for (int p = 1; p < MAXP; p++) player_tick(p);
        if (ticks % 10 == 0 && win_check()) return;
    }
}


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
    }
}
#else
#define ts_check(s)     ((void)0)
#define check_borders() ((void)0)
#endif

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
        for (int p = 1; p < MAXP; p++) player_tick(p);
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

void run_tests(void) {
    ts_test();
    conquer_test();
    blob_test();
    heap_test();
    attack_test();
}
#else
void run_tests(void) {}
#endif

/* ---------------- main ---------------- */

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
    srand(42);
    fill_terrain();
    //print_map();
    run_tests();
    bench();
    return 0;
}