# Faster Development Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip local and CI work that cannot find anything (repeat runs, untouched areas) and add a single-suite loop, without changing what any check verifies.

**Architecture:** One classifier script (`scripts/changed-scope.sh`) is the single source of the docs/web/full rules; `make gates`, the pre-push hook and a CI `changes` job all call it. `make gates` leaves a pass stamp that the hook trusts for the same clean commit.

**Tech Stack:** POSIX sh / bash 3.2 (macOS), GNU make 3.81, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-20-faster-dev-loop-design.md`

## Global Constraints

- Scripts run on macOS bash 3.2 and Ubuntu 24.04; no `mapfile`, `wait -n`, or GNU-only flags.
- Unknown or unmatched paths always classify as `full`. `GATES_SCOPE=full` forces full everywhere.
- Default base: `origin/feature/ws-simplification`, falling back to `origin/main`; overridable by `SCOPE_BASE` or the first argument.
- No change to test content, coverage targets, or `codecov.yml`.

## Review Focus

- A diff mixing `*.md` with one `src/` file → must be `full` (Task 1 test `mixed_docs_and_src_is_full`).
- `locale/en.yaml` alone → `full` (it feeds C and web codegen) (Task 1 test `locale_is_full`).
- Uncommitted edits after `make gates` passed → hook must not trust the stamp (Task 2 step 1, dirty-tree case).
- Base ref missing (fresh clone, no `origin/feature/...`) → falls back, never errors into `docs` (Task 1 test `missing_base_falls_back_full_or_main`).
- CI on a push to `main` → always `full` (Task 4 step 2).

---

### Task 1: `scripts/changed-scope.sh` classifier

**Files:** Create `scripts/changed-scope.sh`, `scripts/test-changed-scope.sh`; Modify `Makefile` (target `test-scripts`, add to `test-all`).

**Interfaces:** Produces `scripts/changed-scope.sh [base]` → prints exactly one of `docs`, `web`, `full` on stdout, exit 0. Files considered: `git diff --name-only <base>...HEAD` plus `git status --porcelain` paths. Honors `GATES_SCOPE` (if set to `docs|web|full`, printed as-is).

- [ ] **Step 1: Write `scripts/test-changed-scope.sh`**: builds a throwaway repo in `mktemp -d` with a `base` branch, then for each case commits files on a topic branch and asserts the output. Cases: `md_only_is_docs` (`README.md`, `docs/x.md`), `agents_and_cursor_is_docs` (`.agents/s/SKILL.md`, `.cursor/rules/a.mdc`), `webui_only_is_web` (`webui/src/a.ts`), `webui_plus_md_is_web`, `mixed_docs_and_src_is_full` (`README.md`, `src/a.c`), `locale_is_full` (`locale/en.yaml`), `makefile_is_full`, `workflow_is_full` (`.github/workflows/test.yml`), `uncommitted_src_is_full` (clean commit of `a.md` + dirty `src/a.c`), `empty_diff_is_docs`, `override_full` (`GATES_SCOPE=full` with md-only), `missing_base_falls_back_full_or_main` (base arg that does not exist and no `origin/main` → `full`).
- [ ] **Step 2: Run** `bash scripts/test-changed-scope.sh` → FAIL (script missing).
- [ ] **Step 3: Implement `scripts/changed-scope.sh`**. Docs pattern: `*.md`, `docs/*`, `.agents/*`, `.cursor/*`, `LICENSE*`. Web: `webui/*`. If the base cannot be resolved (`git rev-parse --verify`), try `origin/main`, else print `full`.
- [ ] **Step 4: Run** the test → all cases PASS. Add `test-scripts: ; bash scripts/test-changed-scope.sh` and make `test-all` depend on it.
- [ ] **Step 5: Commit** `build: classify a change as docs, web or full (scripts/changed-scope.sh)`.

### Task 2: Gates scope and pass stamp; hook uses both

**Files:** Modify `scripts/gates.sh`, `.githooks/pre-push`.

**Interfaces:** Consumes `scripts/changed-scope.sh`. Produces `build/gates/passed` containing the 40-char `HEAD` SHA, written only when all steps pass, scope is `full`, and `git status --porcelain` (excluding `build/`) is empty at both start and end of the run.

- [ ] **Step 1: Hook logic** in `.githooks/pre-push`: compute `scope` with `scripts/changed-scope.sh`; `docs` → print `pre-push: docs-only change, skipping tests` and exit 0; `web` → `cd webui && npm test -- --run`; `full` → if the tree is clean and `build/gates/passed` equals `git rev-parse HEAD`, print `pre-push: make gates already passed for <sha>, skipping` and exit 0; otherwise today's behavior unchanged.
- [ ] **Step 2: Gates logic** in `scripts/gates.sh`: same scope switch at the top (`docs` → print and exit 0; `web` → run web unit tests only); on `full`, write the stamp per the Interfaces rule; delete any old stamp at start.
- [ ] **Step 3: Verify manually** (record output in the PR): (a) md-only commit → `make gates` and `git push --dry-run` hook both skip; (b) after a passing full `make gates`, hook prints the skip line; (c) `touch src/log.c` edit uncommitted → hook runs `test-all`; (d) `GATES_SCOPE=full make gates` on md-only runs all steps.
- [ ] **Step 4: Commit** `build: make gates and the pre-push hook skip what the change cannot affect`.

### Task 3: `make test-suite`

**Files:** Modify `Makefile`, `README_TESTING.md`, `AGENTS.md` (commands table).

**Interfaces:** `make test-suite S=<suite> [C=<tcase>]` → builds `${TEST_BIN}` (default variant), runs `CK_RUN_SUITE=$(S)` (+ `CK_RUN_CASE=$(C)` if set) with `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1`; errors `test-suite: set S=<suite>` when `S` is empty.

- [ ] **Step 1: Verify** `make test-suite S=Log` → `Checks: <n>, Failures: 0`; `make test-suite S=Nope` → non-zero exit with `matched no tests`; `make test-suite` → the `set S=` error.
- [ ] **Step 2: Implement**, document, commit `build: make test-suite S=<suite> runs one suite`.

### Task 4: CI `changes` job and concurrency

**Files:** Modify `.github/workflows/test.yml`.

**Interfaces:** Job `changes` (ubuntu-latest, checkout with `fetch-depth: 0`) outputs `scope`. `c-tests`, `c-asan`, `e2e-fake`: `needs: changes`, `if: needs.changes.outputs.scope == 'full'`. `web-tests`: `if: needs.changes.outputs.scope != 'docs'`. Workflow-level `concurrency: { group: test-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}, cancel-in-progress: ${{ github.event_name == 'pull_request' }} }`.

- [ ] **Step 1: Implement**: `changes` runs `scripts/changed-scope.sh "origin/${{ github.base_ref }}"` on pull requests and echoes `full` on push.
- [ ] **Step 2: Verify**: `actionlint` (Docker `rhysd/actionlint`) reports no new findings; on this PR itself (touches `.github/` → `full`) every job runs.
- [ ] **Step 3: Commit** `ci: skip jobs a change cannot affect; cancel superseded PR runs`.

### Task 5: PR-flow docs (process)

**Files:** Modify `AGENTS.md` (Git and releases), `docs/superpowers/README.md` (add spec + plan rows); memory note `feature-branch-integration-workflow.md` (outside repo).

- [ ] **Step 1:** Document: independent PRs open together; each merges when its own CI incl. Codecov is green; #74 full CI checked before each dev tag and before asking to merge to main. Add `make gates` scope/stamp and `make test-suite` to the AGENTS.md commands table.
- [ ] **Step 2: Commit** `docs: faster-loop workflow`.
