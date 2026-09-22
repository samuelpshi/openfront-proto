/* env steps/sec: puf_step incl. mask build, action = first legal (near-zero policy cost) */
#include "binpack.h"
#include <time.h>
int main(void) {
    static Env e; static obs_t obs[OBS_SIZE]; static unsigned char m[BP_ACT_TOTAL]; float act, rew, term;
    e.rng = 1; e.num_orient = 2; e.stability = 1; e.rng = bp_splitmix32(1);
    e.agents[0] = (Agent){obs, &act, &rew, &term, m, 0};
    puf_reset(&e);
    long N = 2000000; struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    for (long t = 0; t < N; t++) { int k = 0; while (!m[k]) k++; act = (float)k; puf_step(&e); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (double)(b.tv_sec - a.tv_sec) + 1e-9 * (double)(b.tv_nsec - a.tv_nsec);
    printf("%.0f steps/sec, %.0f eps\n", (double)N / s, (double)e.log.n);
}
