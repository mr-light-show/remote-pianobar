# Faster Development Loop: Design (short)

**Intent:** less waiting per change (local gates, pre-push hook, CI, PR merging) while every change is still checked by the same tests before it merges. Approved 2026-11-20 (items 2–6 of the quick-win list).

**Goals:** skip work that cannot find anything (repeat runs, untouched areas); a fast single-suite loop while iterating; overlap PRs.
**Non-goals:** changing what the tests check, the 90% patch-coverage gate, or Codecov.
**Anti-goals:** a change merging without the checks that apply to it; a misclassified change silently skipping C tests.

## Approaches considered

| Approach | Trade-off | Decision |
|---|---|---|
| Native arm64 Docker for the local Linux steps (#1) | ~1–1.5 min faster gates; differs in CPU architecture from CI | Not now (user's call) |
| Skip repeats + change-aware scopes + single-suite loop + CI skip + PR overlap (#2–6) | Path rules must be conservative | **Chosen** |
| Drop local Docker steps, rely on CI | Fast, but Linux failures surface 10 min later | Rejected |

## Design

1. **Gates pass stamp (#2).** `make gates` writes `build/gates/passed` (the tested `HEAD` SHA) when every step passes on a clean working tree. `.githooks/pre-push` skips its `test-all` when the pushed commit equals the stamp and the tree is clean.
2. **Change scope (#3).** `scripts/changed-scope.sh [base]` prints `docs`, `web` or `full` for `git diff --name-only base...HEAD` plus uncommitted changes (base default: `origin/feature/ws-simplification`, falling back to `origin/main`). Rules: every file matches `*.md`, `docs/**`, `.agents/**`, `.cursor/**`, `LICENSE*` → `docs`; every file is under `webui/**` or `docs` → `web`; anything else (including `locale/`, `src/`, `test/`, `Makefile`, `scripts/`, `.github/`, `.githooks/`) → `full`. An empty diff is `docs`. `GATES_SCOPE=full` forces full. `make gates` and the hook use it: docs → nothing; web → `cd webui && npm test -- --run`; full → today's behavior.
3. **Single-suite loop (#4).** `make test-suite S=<suite> [C=<tcase>]` builds `build/default/pianobar_test` and runs `CK_RUN_SUITE=S [CK_RUN_CASE=C]`; it fails when nothing matches (existing `test_main.c` guard).
4. **CI skip (#5).** A `changes` job in `test.yml` runs the same `scripts/changed-scope.sh` against the PR base (`github.base_ref`; pushes to main are always `full`). `c-tests`, `c-asan` and `e2e-fake` run only for `full`; `web-tests` for `web` or `full`. Skipped jobs show as skipped (no branch protection requires them today). `concurrency: {group: test-${{ github.ref }}, cancel-in-progress: true}` for pull requests.
5. **PR flow (#6, process).** Independent PRs are opened together and each merges when its own CI (including Codecov) is green. #74's full CI is checked before each dev tag and before asking to merge to main, not after every merge. Memory note and AGENTS.md updated.

**Left to the builder:** script internals, messages, where the stamp check lives in the hook.
