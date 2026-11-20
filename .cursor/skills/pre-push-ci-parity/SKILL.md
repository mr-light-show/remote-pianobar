---
name: pre-push-ci-parity
description: >-
  Use before git push in remote-pianobar when Makefile, test sources, or CI
  workflows changed. Ensures a clean test build matches GitHub Actions
  (test-coverage), catching compile and headless-integration failures that
  incremental make test-all misses.
---

# Pre-push CI parity — remote-pianobar

## Problem this solves

GitHub Actions **c-tests** runs (headless Ubuntu, **no sound card**):

```bash
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
```

CI starts from a fresh checkout, so every object is compiled from scratch. Locally, `make test-coverage` builds in `build/coverage` and rebuilds only what changed; `make distclean` first gives the clean build.

Local **`make test-all`** (and the default pre-push hook) can miss:

- Wrong `-MF` depfile paths for nested test dirs (`integration/*.d` at repo root)
- Integration tests **timing out** on headless runners (ALSA errors, HTTP fixture `pthread_join` hang, player thread never finishing without `PIANOBAR_TEST_NO_DEVICE`)
- New `TEST_SRC` entries that never compile until a clean build

**Symptoms:** incremental `make test-all` passes on macOS; CI reports compile errors, or `Test timeout expired` in `playback_integration`.

## When to apply

Apply **before push** when **any** of these changed:

- `Makefile`, `test/**`, `src/player.c` (integration / audio init)
- `.github/workflows/test.yml`, `.githooks/pre-push`
- CI failed after a green incremental local run

## Mandatory verification (CI parity)

**Preferred — all gates at once (needs Docker):**

```bash
make gates
```

Runs `make test-ci-local` (Docker, = CI `c-tests`), `make test-coverage`, `make test-all`, `PIANOBAR_CI_TARGET=test-asan make test-ci-local` (= CI `c-asan`), `make test-asan` and `make test-tsan`, at most three at a time, each in its own build directory; a failed step is rerun alone once. Logs: `build/gates/<step>.log`. About 2 minutes from a clean tree.

**Docker repro alone (matches GHA c-tests):**

```bash
make test-ci-local
```

Requires Docker. Runs Ubuntu 24.04 with the same apt packages as CI (prebuilt local image, fresh copy of the tree; the host tree is not written).

**Alternative — on host (needs `lcov` for full parity):**

```bash
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
```

**Minimum (no lcov):**

```bash
make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all
```

**Requirements before push:**

1. Command exits **0**.
2. Integration suite completes without `Test timeout expired`.
3. State in your reply which command you ran.

**Shell timeout:** ≥ **300000 ms** for `make gates`, `make test-ci-local` or clean `test-coverage`.

## Environment variables (CI + integration)

| Variable | Purpose |
|----------|---------|
| `PIANOBAR_INTEGRATION=1` | Run opt-in integration tests |
| `PIANOBAR_TEST_NO_DEVICE=1` | miniaudio `noDevice` mode — required on headless Linux CI |

Always set **both** when reproducing CI locally.

## Quick depfile sanity check (Makefile changes)

```bash
make distclean
make build/default/test/integration/fixture_http.o
ls build/default/test/integration/fixture_http.d build/default/test/integration/fixture_http.o
test ! -e build/default/integration/fixture_http.d
```

Use **`$(@:.o=.d)`**, not **`$*.d`**, in test compile rules with directory components.

## Agent push workflow

```
1. Incremental PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all (dev loop)
2. Before push on test/CI changes → make gates (or make test-ci-local, or the clean test-coverage command above)
3. git push
```

## Related docs

- `scripts/test-ci-local.sh` — Docker CI repro
- `.cursor/rules/pre-push-tests.mdc` — push gate
- `AGENTS.md` — commands table
- `.github/workflows/test.yml` — CI job
