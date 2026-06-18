---
name: pre-push-coverage
description: >-
  Use before git push in remote-pianobar when src/ or webui/src/ production
  code changed. Verifies ≥90% Codecov patch coverage on the diff, matching
  codecov.yml and AGENTS.md.
---

# Pre-push patch coverage — remote-pianobar

## Gate

[codecov.yml](../../codecov.yml) sets **`patch.target: 90%`** (`informational: false`) for:

- `src/**/*.c` (flag `c-tests`)
- `webui/src/**` (flag `web-tests`)

**Agents:** lines you add or modify in those paths must reach **≥90% patch coverage** before push.

## When to apply

Before **`git push`** when the branch changes **production** code under:

- `src/`, `src/libpiano/`, `src/websocket/`
- `webui/src/` (not `webui/test/`)

Skip when the diff is only docs, locale YAML workflow, generated artifacts, or `test/**` with no src changes.

## Verification commands

### C (matches GitHub Actions c-tests job)

```bash
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
```

Requires `lcov`. Produces `coverage.info` at repo root.

**Shell timeout:** ≥ **180000 ms**.

If `lcov` fails on macOS with inconsistent-data warnings, retry capture with ignore (local only):

```bash
lcov --capture --directory . --output-file coverage.info \
  --rc lcov_branch_coverage=1 --ignore-errors inconsistent
lcov --remove coverage.info '/usr/*' '*/test/*' '*l10n_defaults_gen.c' \
  --output-file coverage.info --rc lcov_branch_coverage=1
```

### Web UI

```bash
cd webui && npm test -- --run --coverage
```

### No lcov locally

Run `make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all`, add tests for obvious gaps, push, and confirm Codecov PR comment ≥90% before merge.

## How to read patch coverage

1. **Preferred:** Codecov PR comment → “Files changed” / patch % per file.
2. **Local:** After `make test-coverage`, open PR on Codecov or use `genhtml coverage.info -o coverage-html` and inspect changed `.c` files only.

Focus on **lines you added or changed**, not whole-file project coverage.

## If patch coverage is below 90%

1. Identify missing lines from Codecov (e.g. “5 Missing, 3 partials”).
2. Add **unit tests** in `test/unit/test_*.c` (register in `test/test_main.c`; new file → `TEST_SRC` in `Makefile`).
3. Cover **both branches** of new conditionals (early returns, `#ifdef` paths where build supports them).
4. Re-run `make test-coverage` or wait for Codecov on push.
5. Only if truly untestable: note in PR description (hardware, fork-only paths, etc.).

## Common gaps in this repo

| Pattern | Test approach |
|---------|----------------|
| Early return (`NULL`, CLI vs WEB) | Separate `START_TEST` per branch |
| Playback manager thread branches | Start/stop manager with state that selects park vs timed wait |
| `#ifdef WEBSOCKET_ENABLED` | Tests under same guard in `test/unit/` |
| Pandora HTTP | `BarUiPianoCallSetTestHook` mock |

## Agent push workflow

```
1. Implement feature/fix
2. make test-all (dev loop)
3. If test/** or Makefile changed → pre-push-ci-parity skill
4. If src/** or webui/src/** changed → make test-coverage (this skill)
5. Confirm ≥90% patch on touched production files
6. git push
7. Verify Codecov PR check green
```

## Related

- [.cursor/rules/pre-push-coverage.mdc](../../.cursor/rules/pre-push-coverage.mdc)
- [.cursor/rules/pre-push-tests.mdc](../../.cursor/rules/pre-push-tests.mdc)
- [AGENTS.md](../../AGENTS.md) — Code coverage expectations
- [codecov.yml](../../codecov.yml)
