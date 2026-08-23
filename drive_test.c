/* Exercises the PufferLib interface path (puf_init / puf_reset / puf_step)
   with hand-wired buffers, standing in for what src/pufferl.cu does. The
   openfront.c harness never calls these, so without this file the entire
   binding is unexecuted code. */
#include "openfront.h"

static Dict *mkdict(void) {
    Dict *d = (Dict*)calloc(1, sizeof(Dict));
    d->name = dict_strdup("env");
    d->cap  = 16;
    d->size = 0;
    d->items = (DictItem*)calloc(d->cap, sizeof(DictItem));
    return d;
}

#define NENV 4

int main(int argc, char **argv) {
    int num_agents   = (argc > 1) ? atoi(argv[1]) : 1;
    int agent_is_bot = (argc > 2) ? atoi(argv[2]) : 0;
    int episodes     = (argc > 3) ? atoi(argv[3]) : 30;

    Dict *kw = mkdict();
    dict_set(kw, "num_agents",    num_agents);
    dict_set(kw, "action_repeat", 10);
    dict_set(kw, "max_steps",     200);
    dict_set(kw, "agent_is_bot",  agent_is_bot);

    Env *envs = (Env*)calloc(NENV, sizeof(Env));
    int total_agents = 0;
    for (int i = 0; i < NENV; i++) {
        envs[i].rng = i;                 /* framework does exactly this */
        puf_init(&envs[i], kw);
        total_agents += envs[i].num_agents;
    }

    /* one contiguous block per field, exactly like VecEnv */
    obs_t *obs   = (obs_t*)calloc(total_agents * OBS_SIZE, sizeof(obs_t));
    float *acts  = (float*)calloc(total_agents * NUM_ATNS, sizeof(float));
    float *rews  = (float*)calloc(total_agents, sizeof(float));
    float *terms = (float*)calloc(total_agents, sizeof(float));

    int cursor = 0;
    for (int i = 0; i < NENV; i++) {
        for (int s = 0; s < envs[i].num_agents; s++) {
            envs[i].agents[s].observations = obs   + (size_t)cursor * OBS_SIZE;
            envs[i].agents[s].actions      = acts  + (size_t)cursor * NUM_ATNS;
            envs[i].agents[s].rewards      = rews  + cursor;
            envs[i].agents[s].terminals    = terms + cursor;
            cursor++;
        }
    }

    for (int i = 0; i < NENV; i++) puf_reset(&envs[i]);

    unsigned int seed = 12345;
    long steps = 0, terminals_seen = 0, ep_done = 0;
    float rmin = 1e9f, rmax = -1e9f, omin = 1e9f, omax = -1e9f;
    long act_hist[7] = {0};
    long len_sum = 0;

    while (ep_done < (long)episodes * NENV) {
        for (int a = 0; a < total_agents; a++) {
            seed = seed*1103515245u + 12345u;
            int action = (seed >> 16) % 7;
            acts[a * NUM_ATNS] = (float)action;
            act_hist[action]++;
        }
        for (int i = 0; i < NENV; i++) {
            int before = envs[i].steps;
            puf_step(&envs[i]);
            /* steps resets to 0 on episode end */
            if (envs[i].steps == 0 && before > 0) { ep_done++; len_sum += before + 1; }
        }
        steps++;

        for (int a = 0; a < total_agents; a++) {
            if (rews[a] != rews[a]) { printf("FAIL: NaN reward at agent %d\n", a); return 1; }
            if (rews[a] < rmin) rmin = rews[a];
            if (rews[a] > rmax) rmax = rews[a];
            if (terms[a] != 0.0f) terminals_seen++;
        }
        for (long k = 0; k < (long)total_agents * OBS_SIZE; k++) {
            if (obs[k] != obs[k]) { printf("FAIL: NaN obs at %ld\n", k); return 1; }
            if (obs[k] < omin) omin = obs[k];
            if (obs[k] > omax) omax = obs[k];
        }
        if (steps > 200L * episodes * 4) { printf("FAIL: episodes not terminating\n"); return 1; }
    }

    printf("num_agents=%d agent_is_bot=%d\n", num_agents, agent_is_bot);
    printf("  episodes %ld over %d envs, %ld puf_steps\n", ep_done, NENV, steps);
    printf("  mean episode length %.1f decisions (max_steps 200)\n",
           (double)len_sum / ep_done);
    printf("  reward range [%.4f, %.4f]\n", (double)rmin, (double)rmax);
    printf("  obs range    [%.4f, %.4f]\n", (double)omin, (double)omax);
    printf("  terminals fired %ld\n", terminals_seen);
    printf("  action histogram:");
    for (int i = 0; i < 7; i++) printf(" %ld", act_hist[i]);
    putchar('\n');

    Dict *out = mkdict();
    puf_log(&envs[0].log, out);
    printf("  env0 log: n=%.0f perf=%.4f ep_return=%.4f ep_len=%.1f win=%.0f annex=%.1f\n",
           dict_get(out, "n"),
           dict_get(out, "perf")           / dict_get(out, "n"),
           dict_get(out, "episode_return") / dict_get(out, "n"),
           dict_get(out, "episode_length") / dict_get(out, "n"),
           dict_get(out, "win"),
           dict_get(out, "annexations")    / dict_get(out, "n"));

    if (omin < 0.0f || omax > 1.0f) {
        printf("FAIL: observations outside [0,1]\n");
        return 1;
    }
    for (int i = 0; i < NENV; i++) puf_close(&envs[i]);
    printf("drive ok\n");
    return 0;
}
