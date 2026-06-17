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

`test-coverage` starts with **`make clean`**, then compiles every test object from scratch.

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

**Preferred — Docker repro (matches GHA exactly):**

```bash
make test-ci-local
```

Requires Docker. Runs Ubuntu 24.04 with the same apt packages as CI.

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

**Shell timeout:** ≥ **300000 ms** for `make test-ci-local` or clean `test-coverage`.

## Environment variables (CI + integration)

| Variable | Purpose |
|----------|---------|
| `PIANOBAR_INTEGRATION=1` | Run opt-in integration tests |
| `PIANOBAR_TEST_NO_DEVICE=1` | miniaudio `noDevice` mode — required on headless Linux CI |

Always set **both** when reproducing CI locally.

## Quick depfile sanity check (Makefile changes)

```bash
make distclean
make test/integration/fixture_http.o
ls test/integration/fixture_http.d test/integration/fixture_http.o
test ! -e integration/fixture_http.d
```

Use **`$(@:.o=.d)`**, not **`$*.d`**, in test compile rules with directory components.

## Agent push workflow

```
1. Incremental PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all (dev loop)
2. Before push on test/CI changes → make test-ci-local OR clean test-coverage command above
3. git push
```

## Related docs

- `scripts/test-ci-local.sh` — Docker CI repro
- `.cursor/rules/pre-push-tests.mdc` — push gate
- `AGENTS.md` — commands table
- `.github/workflows/test.yml` — CI job
