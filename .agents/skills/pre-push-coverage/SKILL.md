---
name: pre-push-coverage
description: Use before git push in remote-pianobar when production code under src, src/libpiano, src/websocket, or webui/src changed. Verifies the Codecov requirement of at least 90 percent patch coverage on added or modified production lines.
---

# Pre-push patch coverage for remote-pianobar

Run this workflow from the `remote-pianobar` repository root. The gate applies to changed production lines, not whole-file coverage.

Skip it for diffs limited to documentation, tests, generated locale artifacts, vendored `src/miniaudio_impl.c`, configuration, or workflows.

## C coverage

Run the GitHub Actions equivalent:

```bash
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
```

This requires `lcov`. It builds in `build/coverage` and writes `coverage.info` at the repository root. Allow at least three minutes. Read the patch number from a standalone run: `make gates` also runs coverage, but under that load a few timing-dependent lines can differ.

If local `lcov` is unavailable, run a clean test build, add tests for evident gaps, and require the Codecov PR result to reach 90 percent before merge:

```bash
make distclean
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all
```

## Web UI coverage

```bash
cd webui
npm test -- --run --coverage
```

## Evaluate and respond

Use the Codecov PR files-changed result when available. Locally, use `coverage.info` or an HTML report and scope the assessment to changed production lines.

If patch coverage is below 90 percent, add focused tests for uncovered behavior and rerun coverage. Cover meaningful branches such as early returns and conditional paths. Only document an exception when a line is genuinely impractical to test and the user accepts the tradeoff.

Run `make gates` before the push as well. If `Makefile` or `test/**` also changed, apply the `pre-push-ci-parity` skill before the push. Do not push with a known failing coverage gate. Report the verification command and result.

Read `AGENTS.md` and `codecov.yml` when further context is needed.
