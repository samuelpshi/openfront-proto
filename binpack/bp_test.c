/* binpack test harness. Build with -DDEBUG for the invariant soak (T5),
   with ASan/UBSan for T4. Stands in for pufferl.cu where it matters:
   rng = env index before puf_init, framework-owned buffers incl. the mask. */
#include "binpack.h"

static Dict *mkdict(void) {
    Dict *d = (Dict*)calloc(1, sizeof(Dict));
    d->name = dict_strdup("env");
    d->cap = 16;
    d->items = (DictItem*)calloc(d->cap, sizeof(DictItem));
    return d;
}

typedef struct {
    obs_t obs[OBS_SIZE];
    float act, rew, term;
    unsigned char mask[BP_ACT_TOTAL];
} Bufs;

static void wire(Env *e, Bufs *b) {
    e->agents[0].observations = b->obs;
    e->agents[0].actions = &b->act;
    e->agents[0].rewards = &b->rew;
    e->agents[0].terminals = &b->term;
    e->agents[0].action_mask = b->mask;
}

static void mkenv(Env *e, Bufs *b, int idx, int norient, int stab, int policy) {
    Dict *kw = mkdict();
    dict_set(kw, "num_orientations", norient);
    dict_set(kw, "stability", stab);
    dict_set(kw, "policy", policy);
    memset(e, 0, sizeof(*e));
    e->rng = (unsigned int)idx;          /* framework does exactly this */
    puf_init(e, kw);
    for (int i = 0; i < kw->size; i++) { free(kw->items[i].values); free(kw->items[i].str); }
    free(kw->items); free(kw->name); free(kw);
    wire(e, b);                          /* framework assigns after init */
}

static unsigned long long fnv(const void *p, size_t n, unsigned long long h) {
    const unsigned char *c = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
    return h;
}

static unsigned long long env_hash_x(const Env *e, int with_rng) {
    unsigned long long h = 1469598103934665603ull;
    h = fnv(e->height, sizeof(e->height), h);
    h = fnv(e->voxel, sizeof(e->voxel), h);
    h = fnv(e->instance, sizeof(BpBox) * (size_t)e->n_boxes, h);
    h = fnv(&e->cursor, sizeof(int), h);
    if (with_rng) h = fnv(&e->rng, sizeof(unsigned), h);
    return h;
}
static unsigned long long env_hash(const Env *e) { return env_hash_x(e, 1); }

/* Sampler stand-in: uniform over mask support, own rng. */
static int sample_masked(const unsigned char *m, unsigned int *s) {
    int pop = 0;
    for (int a = 0; a < BP_ACT_TOTAL; a++) pop += m[a];
    if (pop == 0) { printf("FAIL I-M1: sampler saw empty mask\n"); exit(1); }
    int k = bp_below(s, pop);
    for (int a = 0; a < BP_ACT_TOTAL; a++) if (m[a] && k-- == 0) return a;
    return -1;
}

/* ---- T1: oracle replay. Target placement must be legal and rest at tz. ---- */
static int t1_oracle(int n, int norient) {
    static Env e; static Bufs b;
    mkenv(&e, &b, 0, norient, 1, BP_POLICY_NONE);
    int nmin = 1 << 30, nmax = 0; long nsum = 0;
    for (int s = 1; s <= n; s++) {
        bp_reset_seeded(&e, (unsigned int)s);
        if (e.n_boxes < nmin) nmin = e.n_boxes;
        if (e.n_boxes > nmax) nmax = e.n_boxes;
        nsum += e.n_boxes;
        while (1) {
            const BpBox *bx = &e.instance[e.cursor];
            int a = bp_encode(0, bx->tx, bx->ty);
            if (!b.mask[a]) {
                printf("FAIL T1 seed %d box %d: target (%d,%d,%d) masked\n", s, e.cursor, bx->tx, bx->ty, bx->tz);
                bp_print_mask(&e); bp_print_layers(&e); return 1;
            }
            if (e.rest_z[a] != bx->tz) {
                printf("FAIL T1 seed %d box %d: rests at %d, target %d\n", s, e.cursor, e.rest_z[a], bx->tz);
                return 1;
            }
            b.act = (float)a;
            puf_step(&e);
            if (b.term) break;
        }
        if (e.log.n != (float)s || b.rew <= 0.0f) { printf("FAIL T1 seed %d: bad terminal\n", s); return 1; }
    }
    float u = e.log.utilization / e.log.n;
    printf("T1 oracle   %d seeds, norient %d: util %.6f, boxes min %d mean %.1f max %d  %s\n",
           n, norient, (double)u, nmin, (double)nsum / n, nmax, u == 1.0f ? "OK" : "FAIL");
    return u == 1.0f ? 0 : 1;
}

/* Run one full episode on seed s, return final hash. */
static unsigned long long run_episode(Env *e, Bufs *b, unsigned int s, unsigned int *act_rng) {
    bp_reset_seeded(e, s);
    unsigned long long h = 0;
    while (1) {
        b->act = (float)sample_masked(b->mask, act_rng);
        unsigned long long pre = env_hash_x(e, 0);
        puf_step(e);
        if (b->term) return fnv(&pre, sizeof(pre), h);
        h = fnv(&pre, sizeof(pre), h);
    }
}

/* ---- T2: same seed -> bit-identical episode. ---- */
static int t2_determinism(int n) {
    static Env e1, e2; static Bufs b1, b2;
    mkenv(&e1, &b1, 3, 2, 1, BP_POLICY_NONE);
    mkenv(&e2, &b2, 7, 2, 1, BP_POLICY_NONE);
    for (int s = 1; s <= n; s++) {
        unsigned int r1 = 99u + (unsigned)s, r2 = 99u + (unsigned)s;
        if (run_episode(&e1, &b1, (unsigned)s, &r1) != run_episode(&e2, &b2, (unsigned)s, &r2)) {
            printf("FAIL T2 seed %d\n", s); return 1;
        }
    }
    printf("T2 determinism  %d seeds OK\n", n);
    return 0;
}

/* ---- T3: isolation. N envs alone vs round-robin, per-step hash traces. ---- */
#define T3_N 8
#define T3_STEPS 2000
static int t3_isolation(void) {
    static Env e[T3_N]; static Bufs b[T3_N];
    static unsigned long long solo[T3_N][T3_STEPS];
    for (int i = 0; i < T3_N; i++) {
        mkenv(&e[i], &b[i], i, 2, 1, BP_POLICY_NONE);
        puf_reset(&e[i]);
        unsigned int r = 1000u + (unsigned)i;
        for (int t = 0; t < T3_STEPS; t++) {
            b[i].act = (float)sample_masked(b[i].mask, &r);
            puf_step(&e[i]);
            solo[i][t] = env_hash(&e[i]);
        }
    }
    unsigned int r[T3_N];
    for (int i = 0; i < T3_N; i++) {
        mkenv(&e[i], &b[i], i, 2, 1, BP_POLICY_NONE);
        puf_reset(&e[i]);
        r[i] = 1000u + (unsigned)i;
    }
    for (int t = 0; t < T3_STEPS; t++)
        for (int i = 0; i < T3_N; i++) {
            b[i].act = (float)sample_masked(b[i].mask, &r[i]);
            puf_step(&e[i]);
            if (env_hash(&e[i]) != solo[i][t]) { printf("FAIL T3 env %d step %d\n", i, t); return 1; }
        }
    printf("T3 isolation    %d envs x %d steps OK\n", T3_N, T3_STEPS);
    return 0;
}

/* ---- drive: framework path, sampler over mask, obs range, log invariants ---- */
static int drive(long steps) {
    static Env e[4]; static Bufs b[4];
    for (int i = 0; i < 4; i++) { mkenv(&e[i], &b[i], i, 2, 1, BP_POLICY_NONE); puf_reset(&e[i]); }
    unsigned int r = 12345u;
    float omin = 1e9f, omax = -1e9f;
    for (long t = 0; t < steps; t++)
        for (int i = 0; i < 4; i++) {
            b[i].act = (float)sample_masked(b[i].mask, &r);
            puf_step(&e[i]);
            for (int k = 0; k < OBS_SIZE; k++) {
                float o = b[i].obs[k];
                if (o != o) { printf("FAIL drive: NaN obs\n"); return 1; }
                if (o < omin) omin = o;
                if (o > omax) omax = o;
            }
        }
    Log L; memset(&L, 0, sizeof(L));
    for (int i = 0; i < 4; i++) {
        float *s = (float*)&e[i].log, *d = (float*)&L;
        for (size_t k = 0; k < sizeof(Log) / sizeof(float); k++) d[k] += s[k];
    }
    float n = L.n;
    float causes = (L.mask_cause_orient + L.mask_cause_footprint + L.mask_cause_height + L.mask_cause_support) / n;
    float legal = L.mean_legal_actions / n;
    float closure = causes + legal / (float)BP_ACT_TOTAL;      /* must be 1 */
    printf("drive  %ld steps x4, %.0f eps: util %.3f placed %.1f remaining %.1f legal %.1f | "
           "orient %.3f foot %.3f height %.3f support %.3f | closure %.6f obs [%.2f, %.2f]\n",
           steps, (double)n, (double)(L.utilization / n), (double)(L.boxes_placed / n),
           (double)(L.boxes_remaining / n), (double)legal,
           (double)(L.mask_cause_orient / n), (double)(L.mask_cause_footprint / n),
           (double)(L.mask_cause_height / n), (double)(L.mask_cause_support / n),
           (double)closure, (double)omin, (double)omax);
    if (omin < 0.0f || omax > 1.0f) { printf("FAIL drive: obs out of [0,1]\n"); return 1; }
    if (closure < 0.9999f || closure > 1.0001f) { printf("FAIL drive: cause closure\n"); return 1; }
    if (L.illegal_actions != 0.0f) { printf("FAIL drive: masked sampler produced illegal actions\n"); return 1; }
    return 0;
}

/* ---- untrained-eval path: action 0 every step, as puffercpu.c sends with
   no checkpoint. Must not corrupt state (debug build runs bp_check_state
   every step) and must count every illegal action. ---- */
static int t_illegal(int n) {
    static Env e; static Bufs b;
    mkenv(&e, &b, 0, 2, 1, BP_POLICY_NONE);
    puf_reset(&e);
    long steps = 0, illegal_seen = 0;
    while (e.log.n < (float)n) {
        illegal_seen += !b.mask[0];
        b.act = 0.0f;
        puf_step(&e);
        steps++;
    }
    float per_dec = e.log.illegal_actions / e.log.n;
    printf("T-illegal  %d eps, action 0 always: util %.4f illegal/decision %.3f (saw %ld of %ld)  %s\n",
           n, (double)(e.log.utilization / e.log.n), (double)per_dec, illegal_seen, steps,
           illegal_seen > 0 ? "OK" : "FAIL");
    return illegal_seen > 0 ? 0 : 1;
}

/* ---- baselines on EVAL_SEEDS, through the in-env policy path ---- */
static void baseline(int policy, const char *name, int norient, int stab, int nseeds) {
    static Env e; static Bufs b;
    mkenv(&e, &b, 0, norient, stab, policy);
    double sum = 0, sq = 0, placed = 0;
    for (int s = 1; s <= nseeds; s++) {
        bp_reset_seeded(&e, (unsigned int)s);
        float before = e.log.utilization, pb = e.log.boxes_placed;
        do puf_step(&e); while (!b.term);
        double u = e.log.utilization - before;
        sum += u; sq += u * u; placed += e.log.boxes_placed - pb;
    }
    double m = sum / nseeds, sd = sqrt(sq / nseeds - m * m);
    printf("  %-8s util %.4f (sd %.3f, se %.4f)  placed %.1f\n",
           name, m, sd, sd / sqrt((double)nseeds), placed / nseeds);
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 5000;
    printf("sizeof(Env) = %zu, OBS_SIZE %d, ACT %d\n", sizeof(Env), OBS_SIZE, BP_ACT_TOTAL);
    int fail = 0;
    fail |= t1_oracle(n, 2);
    fail |= t1_oracle(n, 6);
    fail |= t2_determinism(n / 5);
    fail |= t3_isolation();
    fail |= drive(n * 10L);
    fail |= t_illegal(n / 5);
    if (fail) { printf("FAILED\n"); return 1; }
    const char *nm[] = {"none", "random", "dbl", "flat"};
    for (int st = 1; st >= 0; st--)
        for (int no = 2; no <= 6; no += 4) {
            printf("baselines, seeds 1..1000, SIZE %d dims [%d,%d], stability %d, orient %d, preview %d:\n",
                   BP_SIZE, BP_MIN_DIM, BP_MAX_DIM, st, no, BP_PREVIEW_K);
            for (int p = 1; p <= 3; p++) baseline(p, nm[p], no, st, 1000);
        }
    printf("ALL OK\n");
    return 0;
}
