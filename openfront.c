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
