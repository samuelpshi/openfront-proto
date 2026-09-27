# Vast.ai PufferLib Setup — Reference

> **PARTLY STALE — 4.0 CONTENT BELOW, 5.0 ANSWERS UP HERE (Sept 2026).** The body documents a **4.0** fork, and 5.0 purged Python entirely (`92e321a5 Purge python`). Every `pip install` / `puffer train` / Python-config step below is dead. The apt block and the GPU/host-selection advice are still correct; treat the rest as history.
>
> **5.0 workflow, verified on a live 3090 instance 17 Sept 2026:**
>
> ```bash
> git clone --depth 1 -b 5.0 git@github.com:samuelpshi/PufferLib.git
> cd PufferLib
> ./build.sh openfront          # -> ./puffer  (needs nvcc + ccache)
> ./puffer train                # reads config/openfront.ini
> ```
>
> The env is *compiled into* the trainer — switching envs means rebuilding, not changing a flag. Overrides are `--section.key=value` (equals sign, underscore in the key): `./puffer train --train.total_timesteps=100000`.
>
> **Open questions from the last revision, now answered:**
> - *Is `ccache` preinstalled?* The apt block below installs it, and that block is still the right first move — 5.0's `native` build invokes `ccache` unconditionally and dies at `build.sh` line 506 without it.
> - *How is `config/<env>.ini` loaded, and what does a malformed one look like?* It loads natively and `[env]` keys reach `puf_init`. Verify per run by checking epoch 1's entropy and build counters (never its perf or length, which are death-biased) rather than trusting the ini — a silently-defaulted key produces a plausible-looking curve on the wrong problem. See §6.2 of `openfront_project_reference.md`.
> - *Does the "Mac can't eval CUDA checkpoints" limit survive?* **No — dead (21 Sept 2026).** The Mac `--cpu` build loads and renders Vast `.bin` checkpoints.
>
> **21 Sept 2026 — second verified instance, different template (CUDA 13.2, `/workspace`, `/venv/main` Python env).** Additions to the flow above:
>
> - **NCCL: check before fixing.** The trainer links `-lnccl` even on one GPU. Two templates have been seen. One has system NCCL (`/usr/lib/x86_64-linux-gnu/libnccl.so`), and no fix is needed. The other ships NCCL only as a pip package; find it with `find / -name "libnccl.so*"` and apply the symlink + export below only then. The pip-only template ships it (`/venv/main/lib/python3.12/site-packages/nvidia/nccl/lib/libnccl.so.2`) with no unversioned `.so`, so the link fails with `cannot find -lnccl`. Fix used: `D=<that dir>; ln -s $D/libnccl.so.2 $D/libnccl.so; export LIBRARY_PATH=$D:$LIBRARY_PATH LD_LIBRARY_PATH=$D:$LD_LIBRARY_PATH` — exports are per-pane, so the export must be live in the pane that runs `./puffer`. Worked at runtime; the pip package's CUDA major (cu12 vs cu13) was not checked. Cleaner alternative: `apt install -y libnccl2 libnccl-dev` (needs NVIDIA's cuda-keyring repo). RTX 3090 (sm_86) is fine on CUDA 13.
> - **Clone over HTTPS** (`https://github.com/samuelpshi/PufferLib.git`) — no SSH key needed for a train-only box.
> - **Run under tmux** so an SSH drop doesn't kill the run. Add `tmux` to the apt block if the template lacks it. The templates seen so far auto-start tmux, and `tmux new` then errors with "sessions should be nested". Open a second window instead (`Ctrl-b c`); a new window needs the NCCL exports again.
> - **Pipe every run through `tee run_<tag>.log`.** With stdout not a tty, the dashboard appends plain snapshots, so every run's final block survives sequential runs.
> - **`config/openfront.ini` now has `device = cuda`**; GPU still reads 3–7% (6–26% at 2×512) because the env is 81–94% of the loop. Low GPU% is not the device bug — check the ini.
> - **Pass run length on the CLI:** `./puffer train --train.total_timesteps=100000000`. The ini default is 10M. Seed: `--base.seed=N` (default 73). Policy shape: `--policy.hidden_size=512 --policy.num_layers=2`. Throughput varies up to ~2× by host; see §6.3 of the project reference.
> - **Collecting checkpoints:** one timestamped dir per run under `checkpoints/openfront/`, names sort chronologically, and the last `.bin` in each is the final. Copy finals to `/tmp/ck` with a loop that skips empty checkpoint dirs:
>
>   ```bash
>   for d in checkpoints/openfront/*/; do f=$(ls "$d"*.bin 2>/dev/null | tail -1); [ -n "$f" ] && cp "$f" /tmp/ck/"$(basename "$d")".bin; done
>   ```
>
>   Also copy `run_*.log` and `logs/openfront/*.ini`; each `.ini` holds the seed and the full binned curve. Map seeds with `grep -H "^seed = 7" *.ini`. Then from the Mac `scp -P <port> 'root@<host>:/tmp/ck/*' <dest>` — capital `-P`, port and host from the console's connect (`>_`) command, and quote the remote glob. Check sizes: 1×128 ≈ 200 KB, 2×512 ≈ 6 MB.
> - **Mac eval works** — see `openfront_project_reference.md` §6.3.
> - **Decide on follow-up runs before destroying an instance.**
>
> **Two things that waste instance time — don't:**
> - `./puffer eval` opens a render window and **segfaults headless** (GLFW fails on missing `DISPLAY`, unchecked before dereference). `xvfb-run -a` initializes but prints no metrics. Eval locally.
> - Check `device` in the ini before a long run. A `device = cpu` default leaves the GPU at 3% for the entire run.

Captured from the Flappy build/train session (4.0 branch). The earlier `puffer_squared` run on Vast apparently didn't hit most of this — either a different base image or one that happened to have these preinstalled. Treat this as the checklist to run on *any* fresh instance before assuming the toolchain is ready.

---

## Run this first, every fresh instance

```bash
apt update && apt install -y clang ccache libomp-dev \
    libgl1-mesa-dev libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev
```

This single block preempts every missing-toolchain error encountered:

| Package | Why PufferLib needs it | Error without it |
|---|---|---|
| `clang` | `build.sh` hardcodes this compiler by name (box shipped `gcc` instead) | `clang: command not found` |
| `ccache` | Build script optionally shells out to it to speed up repeat compiles | `ccache: command not found` |
| `libomp-dev` | OpenMP runtime — needed for `#pragma omp` parallelism in `src/vecenv.h` | `cannot find -lomp` |
| `libgl1-mesa-dev` | OpenGL — needed by raylib (`c_render`'s `InitWindow`/`DrawRectangle`) | `cannot find -lGL` |
| `libx11-dev`, `libxcursor-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxi-dev` | raylib's standard Linux windowing dependency chain | would surface one at a time (`-lX11`, `-lXcursor`, etc.) if installed incrementally |

The Vast.ai "PyTorch (Vast)" template is optimized for Python/CUDA workflows and apparently doesn't ship a native C/graphics build toolchain by default — none of the above reflects on env code correctness, it's purely box provisioning.

---

## SSH key setup (every fresh instance, no exceptions)

Keys do **not** persist across instance destroy/recreate. Each new box needs its own keypair registered with GitHub before cloning will work.

```bash
ssh-keygen -t ed25519 -C "vast-box"
cat ~/.ssh/id_ed25519.pub
# → paste into GitHub: Settings → SSH and GPG keys → New SSH key
```

Then:
```bash
git clone --branch 4.0 git@github.com:samuelpshi/PufferLib.git
cd PufferLib
pip install -e .
```

No Docker / PufferTank wrapper needed — confirmed unnecessary on cloud x86 Linux, that wrapper only exists to paper over Mac-specific pain.

---

## Repo-specific facts (PufferLib 4.0, this fork's branch)

These aren't missing packages — they're structural/naming requirements of the repo itself that aren't obvious from older docs or file-header comments.

- **`scripts/build_ocean.sh` does not exist on this branch.** It's a stale reference left over in old example file headers (e.g. comments copied from `squared.c`/`flappy.c`). Don't trust it.
- **Real build entry point** is at the repo root:
  ```bash
  ./build.sh ENV_NAME [--float] [--debug] [--local|--fast|--cpu|--web|--profile]
  ```
  - `--local` → standalone debug executable with sanitizers (ASan/UBSan). Fast to build, best error messages — use this first to validate env logic before touching the full training stack.
  - (no flag) → builds `pufferlib/_C.*.so`, the real CUDA-backed native training extension that `puffer train` actually needs.
  - `--cpu` → CPU-only fallback (Mac/no-GPU path, not relevant on a CUDA box).
- **New env directory structure is rigid.** For an env named `<name>`:
  - Must live at `ocean/<name>/`
  - Files inside must be named **exactly** `<name>.h` and `<name>.c` (not arbitrary names — `flappy.c` inside `ocean/flappy_basic/` fails; renaming the whole directory to match is the cleanest fix) plus a `binding.c`.
- **Training requires a config file that doesn't get created automatically.** `puffer train <name>` reads `config/<name>.ini`, specifically checking that `<name>` appears in the `[base]` section's `env_name` field. Missing this gives `ValueError: No config for env_name <name>`, not a build error — easy to mistake for something else since it only surfaces at train time, after a successful build.

Minimal template (adapt hyperparameters from an existing similar env, e.g. `config/squared.ini`):
```ini
[base]
env_name = <name>

[vec]
total_agents = 4096
backend = Serial

[policy]
hidden_size = 128
num_layers = 1

[train]
total_timesteps = 1_000_000
gamma = 0.99
learning_rate = 0.005
minibatch_size = 32768
ent_coef = 0.01
```
(Omit `[env]` entirely if `my_init` in `binding.c` reads no env-specific kwargs — an empty section with no keys isn't needed.)

---

## Quick triage order if a build fails

1. Compiler missing → install `clang`
2. Linker can't find `-l<name>` → install the matching `-dev` package (OpenMP → `libomp-dev`, GL → `libgl1-mesa-dev`, X11 variants → the `libx*-dev` set above)
3. `./build.sh ENV_NAME` can't find `ocean/ENV_NAME/ENV_NAME.c` → directory/file naming mismatch, rename to match
4. `puffer train` raises `ValueError: No config for env_name` → missing `config/<name>.ini`
5. `ImportError: cannot import name '_C'` → native extension was never built; run `./build.sh ENV_NAME` (no flags) before `puffer train`
