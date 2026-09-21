#include "openfront.h"

// harness

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

static int cmp_int_asc(const void *a, const void *b) {
    return *(const int*)a - *(const int*)b;
}

static void spawn_sweep(void) {
    typedef struct {
        int seed, hard_fail, relax_spawns, max_depth, total_tries, comp_size;
    } MapStat;

    static MapStat stats[1000];
    static int comp_sorted[1000];
    int total_hard = 0, maps_with_relax = 0;
    long total_tries_all = 0;
    int depth_hist[2] = {0, 0};
    int mismatches = 0;

    Env *e_ref = (Env*)malloc(sizeof(Env));
    if (!e_ref) { printf("spawn_sweep: oom\n"); exit(1); }

    for (int seed = 0; seed < 1000; seed++) {
        /* Reference path: real sim_reset, captures spawn_center for each player. */
        sim_init(e_ref, (unsigned int)seed);
        sim_reset(e_ref);
        int ref_centers[MAXP];
        for (int p = 0; p < MAXP; p++) ref_centers[p] = e_ref->spawn_center[p];

        /* Sweep path: identical setup sequence to sim_reset. */
        Env *e = (Env*)malloc(sizeof(Env));
        if (!e) { printf("spawn_sweep: oom\n"); exit(1); }
        sim_init(e, (unsigned int)seed);
        e->land_frac = 0.65f;
        fill_terrain(e);
        players_reset(e);
        bots_init(e);
        for (int i = 0; i < MAXATK; i++) e->attacks[i].active = 0;
        for (int p = 0; p < MAXP; p++) e->spawn_center[p] = -1;

        int hard_fail = 0, relax_spawns = 0, max_depth = 0, tries = 0;

        for (int p = 1; p < MAXP; p++) {
            int placed = 0;
            for (int attempt = 0; attempt < SPAWN_TRIES; attempt++) {
                int cx = rng_below(e, OF_W), cy = rng_below(e, OF_H);
                tries++;
                int c = ref(cx, cy);
                if (!is_land(e, c) || e->owner[c] != 0 ||
                        e->cl_comp[c] != e->largest_comp) continue;
                int nb[4];
                int nn = neighbors(c, nb), touching = 0;
                for (int k = 0; k < nn; k++)
                    if (e->owner[nb[k]] != 0) { touching = 1; break; }
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

                int depth = (attempt >= SPAWN_RELAX) ? 1 : 0;
                if (depth) relax_spawns++;
                if (depth > max_depth) max_depth = depth;

                for (int si = -SPAWN_RADIUS; si <= SPAWN_RADIUS; si++)
                    for (int sj = -SPAWN_RADIUS; sj <= SPAWN_RADIUS; sj++) {
                        if (si*si + sj*sj > SPAWN_RADIUS*SPAWN_RADIUS) continue;
                        conquer(e, p, ref(cx + sj, cy + si));
                    }
                e->spawn_center[p] = c;
                placed = 1;
                break;
            }
            if (!placed) hard_fail++;
        }

        /* Compare sweep spawn_center against real sim_reset. */
        for (int p = 1; p < MAXP; p++) {
            if (e->spawn_center[p] != ref_centers[p]) {
                printf("MISMATCH seed=%d player=%d sweep=%d ref=%d\n",
                       seed, p, e->spawn_center[p], ref_centers[p]);
                mismatches++;
            }
        }

        stats[seed].seed         = seed;
        stats[seed].hard_fail    = hard_fail;
        stats[seed].relax_spawns = relax_spawns;
        stats[seed].max_depth    = max_depth;
        stats[seed].total_tries  = tries;
        stats[seed].comp_size    = e->cl_size[e->largest_comp];

        total_hard      += hard_fail;
        total_tries_all += tries;
        if (relax_spawns > 0) maps_with_relax++;
        depth_hist[max_depth > 0 ? 1 : 0]++;
        free(e);
    }

    free(e_ref);

    if (mismatches > 0) {
        printf("%d mismatches over 8000 slots — copy is not faithful\n", mismatches);
        return;
    }
    printf("copy faithful: 0 mismatches over 8000 slots\n");

    for (int i = 0; i < 1000; i++) comp_sorted[i] = stats[i].comp_size;
    qsort(comp_sorted, 1000, sizeof(int), cmp_int_asc);

    printf("=== spawn sweep: seeds 0-999, 8 players, land_frac=0.65 ===\n");
    printf("hard spawn failures: %d/8000 slots (%.2f%%)\n",
           total_hard, 100.0 * total_hard / 8000);
    printf("maps with any relaxed spawn: %d/1000 (%.1f%%)\n",
           maps_with_relax, maps_with_relax * 0.1);
    printf("relaxation depth histogram: depth=0 %d maps, depth=1 %d maps\n",
           depth_hist[0], depth_hist[1]);
    printf("total spawn attempts: %ld (mean %.1f/map, mean %.1f/player)\n",
           total_tries_all, (double)total_tries_all / 1000,
           (double)total_tries_all / 8000);
    printf("largest-comp size: min=%d  p1=%d  median=%d\n",
           comp_sorted[0], comp_sorted[9], comp_sorted[499]);

    printf("5 worst maps (relax_spawns DESC, comp_size ASC):\n");
    int used[1000];
    memset(used, 0, sizeof(used));
    for (int shown = 0; shown < 5; shown++) {
        int best = -1;
        for (int i = 0; i < 1000; i++) {
            if (used[i]) continue;
            if (best < 0) { best = i; continue; }
            if (stats[i].relax_spawns > stats[best].relax_spawns)
                { best = i; continue; }
            if (stats[i].relax_spawns == stats[best].relax_spawns &&
                    stats[i].comp_size < stats[best].comp_size)
                { best = i; continue; }
        }
        used[best] = 1;
        printf("  seed %4d: hard=%d relax=%d depth=%d tries=%d comp=%d\n",
               stats[best].seed, stats[best].hard_fail, stats[best].relax_spawns,
               stats[best].max_depth, stats[best].total_tries, stats[best].comp_size);
    }
}

static void map_dump(int n, unsigned int seed) {
    for (int m = 0; m < n; m++) {
        Env *e = (Env*)malloc(sizeof(Env));
        if (!e) { printf("map_dump: oom\n"); exit(1); }
        sim_init(e, seed + (unsigned int)m);
        e->land_frac = 0.65f;
        fill_terrain(e);
        players_reset(e);
        printf("--- map %d (seed %u) ---\n", m + 1, seed + (unsigned int)m);
        print_owner(e);
        free(e);
    }
}

int main(int argc, char **argv) {
    printf("sizeof(Env) = %zu bytes (%.2f MB)\n",
           sizeof(Env), sizeof(Env) / (1024.0*1024.0));
    run_tests();
    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        bench(42);
    } else if (argc > 1 && strcmp(argv[1], "dump") == 0) {
        map_dump(20, 1);
    } else if (argc > 1 && strcmp(argv[1], "spawn") == 0) {
        spawn_sweep();
    } else {
        hist_run(300, 2000, 42);
    }
    return 0;
}
