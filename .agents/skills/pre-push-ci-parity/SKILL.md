---
name: pre-push-ci-parity
description: Use before git push in remote-pianobar when build files, tests, player audio initialization, CI workflows, or the pre-push hook changed, or when CI failed after an incremental local pass. Runs a clean headless test build matching GitHub Actions.
---

# Pre-push CI parity for remote-pianobar

Run this workflow from the `remote-pianobar` repository root. It catches clean-build, nested depfile, audio-device, and integration-test failures that an incremental `make test-all` can miss.

## Trigger paths

Apply before a requested push when changes include any of:

- `Makefile`
- `test/**`
- `src/player.c`
- `.github/workflows/test.yml`
- `.githooks/pre-push`

Also apply when CI fails after a green incremental local run.

## Verification

Prefer running every gate at once when Docker is available:

```bash
make gates
```

It runs `make test-ci-local` (Docker, matches CI `c-tests`), `make test-coverage`, `make test-all`, `PIANOBAR_CI_TARGET=test-asan make test-ci-local` (matches CI `c-asan`), `make test-asan` and `make test-tsan`, at most three at a time, each in its own `build/<variant>` directory. A failed step is rerun alone once. Logs are in `build/gates/<step>.log`. It takes about two minutes from a clean tree. Use `GATES_STEPS` to pick steps, for example `GATES_STEPS="coverage test-all asan tsan" make gates` without Docker.

The Docker reproduction alone is `make test-ci-local`. It uses a prebuilt local image and a fresh copy of the tree, and never writes to the host tree.

Otherwise run the host coverage build when `lcov` is available:

```bash
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
```

The minimum fallback is:

```bash
make distclean
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all
```

Allow at least five minutes. Both environment variables are required for headless CI parity. Before pushing, require an exit status of zero and an integration suite that finishes without `Test timeout expired`. Report which command ran.

If verification fails, do not push. Diagnose or report the failure, then rerun the selected command after a fix.

For Makefile depfile changes, optionally verify the invariant directly:

```bash
make distclean
make build/default/test/integration/fixture_http.o
ls build/default/test/integration/fixture_http.d build/default/test/integration/fixture_http.o
test ! -e build/default/integration/fixture_http.d
```

Test compile rules containing directory components must use `$(@:.o=.d)`, not `$*.d`.

Read `AGENTS.md`, `.github/workflows/test.yml`, and `scripts/test-ci-local.sh` when further context is needed.
