---
name: pre-push-ci-parity
description: >-
  Use before git push in remote-pianobar when Makefile, test sources, or CI
  workflows changed. Ensures a clean test build matches GitHub Actions
  (test-coverage), catching nested test path / depfile compile failures that
  incremental make test-all misses.
---

# Pre-push CI parity — remote-pianobar

## Problem this solves

GitHub Actions **c-tests** runs:

```bash
PIANOBAR_INTEGRATION=1 make test-coverage
```

`test-coverage` starts with **`make clean`**, then compiles every test object from scratch.

Local **`make test-all`** (and the default pre-push hook) reuses existing `.o` / `.d` files. That hides Makefile bugs such as:

- Wrong `-MF` depfile paths for nested test dirs (`integration/*.d` at repo root instead of `test/integration/*.d`)
- Missing `test/<subdir>/%.o` compile rules for new test trees
- New `TEST_SRC` entries that never compile until a clean build

**Symptom:** `make test-all` passes locally, CI fails on first compile of `test/integration/*.c`.

## When to apply

Apply this skill **before claiming push-ready** when **any** of these changed in your branch:

- `Makefile` (especially `TEST_SRC`, `test/*` compile rules, `-MMD`/`-MF`)
- `test/**` (new suites, dirs, or moved sources)
- `.github/workflows/test.yml` or integration env vars (`PIANOBAR_INTEGRATION`)
- `.githooks/pre-push`

Also apply when CI failed with compile errors under `test/` after a green incremental local run.

## Mandatory verification (CI parity)

From repo root, run **one** of:

```bash
# Best match for CI (needs lcov installed)
PIANOBAR_INTEGRATION=1 make test-coverage
```

```bash
# Minimum when lcov is unavailable — still clean-compiles all tests
make distclean && PIANOBAR_INTEGRATION=1 make test-all
```

**Requirements before push:**

1. Command exits **0**.
2. You saw test objects compile from scratch (after `distclean` / `clean`), not only link.
3. State in your reply which command you ran.

**Shell timeout:** allow ≥ **180000 ms** (clean build + integration tests is slower than incremental `test-all`).

## Quick depfile sanity check (Makefile changes)

After editing test compile rules, confirm depfiles sit **next to objects**:

```bash
make distclean
make test/integration/fixture_http.o   # example; use any new nested test .c
ls test/integration/fixture_http.d test/integration/fixture_http.o
test ! -e integration/fixture_http.d      # must NOT exist at repo root
```

For every `test/<subdir>/` test tree, prefer:

```makefile
test/<subdir>/%.o: test/<subdir>/%.c
	$(CC) -c -o $@ ... -MMD -MF $(@:.o=.d) -MP $<
```

Use **`$(@:.o=.d)`**, not **`$*.d`**, when the pattern has directory components.

## Agent push workflow (with this skill)

```
1. Normal edits
2. Incremental make test-all (fast feedback during development)
3. If test infra changed → run CI-parity command above
4. git push (hook also runs clean build when Makefile/test/** changed)
5. Reply must cite parity command if step 3 applied
```

## Do not

- Push after only incremental `make test-all` when `Makefile` or `test/**` changed.
- Assume the pre-push hook’s default incremental run equals CI.
- Use `git push --no-verify` to skip parity unless the user explicitly requests it.

## Related docs

- `.cursor/rules/pre-push-tests.mdc` — always-on push gate
- `AGENTS.md` — Build and test commands
- `.github/workflows/test.yml` — CI job definition
