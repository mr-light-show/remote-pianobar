# Agent guide — remote-pianobar

Instructions for AI coding agents working in this repository. For human-oriented setup, see [DEVELOPMENT.md](DEVELOPMENT.md).

## Related repositories

| Repository | When to change it |
|------------|-------------------|
| [remote-pianobar-ha](https://github.com/mr-light-show/remote-pianobar-ha) | Home Assistant entities, services, config flow, browse media |
| [remote_pianobar_card](https://github.com/mr-light-show/remote_pianobar_card) | Lovelace card UI and editor |

Player protocol strings and shared concepts start here (`locale/en.yaml`), then sync to HA/card manually when the same user-facing concept appears there. See each repo’s `AGENTS.md`.

---

## Workflow checklist

1. Confirm you are in **this** repo (`remote-pianobar`) unless the task is HA- or card-only.
2. Make a **minimal** change: match surrounding module style; no drive-by refactors.
3. Run the **verification commands** below for every surface you touched.
4. **User-facing text:** edit `locale/en.yaml`, then `make locale-codegen`. Do not hand-edit generated locale artifacts.
5. **Before `git push`:** run `make test-all` from the repo root (allow ≥120s). Do not push if it fails.
   - If **`Makefile`**, **`test/**`**, or **`.github/workflows/test.yml`** changed: run **`make test-ci-local`** (Docker) or **`make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all`**. See `.cursor/skills/pre-push-ci-parity/SKILL.md`.
6. **Commits and pushes** only when the user explicitly asks.

---

## Repository layout

```
src/                    # Core C (main, player, ui*, settings, l10n, …)
src/libpiano/           # Pandora HTTP client
src/websocket/          # Remote API (core, http, protocol, daemon)
test/unit/              # C unit tests (Check framework)
webui/src/              # Lit + TypeScript web UI
locale/*.yaml           # Canonical i18n source (en.yaml is canonical English)
```

**Generated — do not edit by hand:** `src/l10n_defaults_gen.c`, `locale/<lang>` (key=value), `webui/src/locales/<lang>.json`. Regenerate with `make locale-codegen`.

**Lint exclusions:** `src/miniaudio_impl.c` is omitted from cppcheck source set; treat as vendored.

---

## Build and test commands

| Task | Command |
|------|---------|
| Locale codegen (after YAML changes) | `make locale-codegen` |
| Build player | `make` |
| C unit tests | `make test` |
| **Pre-push gate** (tests + cppcheck) | `make test-all` |
| **Pre-push CI parity** (after `Makefile` / `test/**` changes) | `make test-ci-local` or `make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all` |
| C coverage (CI-style) | `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage` (requires `lcov`) |
| Docker CI repro | `make test-ci-local` |
| C static analysis | `make lint` / `make lint-test` |
| AddressSanitizer tests | `make test-asan` |
| Web UI unit tests | `cd webui && npm ci && npm test -- --run` |
| Web UI coverage | `cd webui && npm test -- --run --coverage` |
| Unified build + test | `./build.sh --test` |

**Note:** Local pre-push hook runs `make test-all`, or **`make distclean && PIANOBAR_INTEGRATION=1 make test-all`** when pushed commits touch `Makefile` or `test/**`. GitHub Actions C job runs `PIANOBAR_INTEGRATION=1 make test-coverage` (clean build). Run the CI-parity command when you change test infrastructure — see `.cursor/skills/pre-push-ci-parity/SKILL.md`.

Tests are disabled when building with `NOWEBSOCKET=1`.

**Git hooks (one-time):** `git config core.hooksPath .githooks` — pre-push runs `make test-all`.

---

## Testing policy

### When to add tests

- New or changed behavior in C modules → add or extend `test/unit/test_*.c`, register suite in `test/test_main.c`, add file to `TEST_SRC` in `Makefile` if new.
- Bug fixes → add a regression test when the failure is unit-testable.
- Web UI logic/components → Vitest under `webui/test/`.
- Skip tests only if the user explicitly accepts that tradeoff.

### How to write good tests

- One behavior per `START_TEST`; descriptive names (e.g. `test_l10n_format_with_placeholder`).
- Cover **NULL** and edge cases for public APIs.
- Keep the suite fast; clean up temp files and state.
- Use Check assertions (`ck_assert`, `ck_assert_str_eq`, `ck_assert_ptr_null`, …).
- For `ui_act.c` handlers, follow patterns in `test/unit/test_ui_act_reconnect.c`.

Template and Makefile steps: [README_TESTING.md](README_TESTING.md).

### What not to test

- Trivial accessors with no logic.
- Generated `l10n_defaults_gen.c` (excluded from coverage reports).
- Vendored miniaudio wrapper.

---

## Code coverage expectations

- CI uploads C (`coverage.info`, flag `c-tests`) and web (`webui/coverage`, flag `web-tests`) to Codecov.
- [codecov.yml](codecov.yml) enforces **≥90% patch coverage** on PRs (`informational: false`); project-wide status stays off.
- **Requirement for agents:** **lines you add or modify must have ≥90% line coverage** (patch/diff coverage), matching the Codecov gate.
- **Applies to:**
  - **C:** touched `.c` / `.h` under `src/`, `src/libpiano/`, `src/websocket/`.
  - **Web UI:** touched files under `webui/src/` (not `webui/test/`).
- **Verify before claiming done:**
  1. `make test-coverage` and/or `cd webui && npm test -- --run --coverage`.
  2. Confirm **≥90% on your diff** (Codecov PR comment, or coverage report scoped to changed files).
  3. If below 90%, add tests or document in the PR why remaining lines are not reasonably testable.
- **Excluded from the 90% target:** generated locale files, vendored `miniaudio_impl.c`, **test code** (`test/**`, `webui/test/**`), config-only or workflow-only edits.

---

## C coding style and readability

Follow existing code in the module you edit.

- **Naming:** `Bar*` types and functions (`BarL10nGet`, `BarPlayerInit`, `BarUiAct*`, `BarState*`).
- **Formatting:** GNU-style spacing — space before `(` in definitions, e.g. `bool BarL10nInit (BarL10nContext_t *ctx, ...)`.
- **Headers:** `#pragma once`; use the project copyright block when sibling files have it.
- **Errors:** Match the module’s convention — see [ERROR_HANDLING.md](ERROR_HANDLING.md) (`bool`, `0`/nonzero, `PianoReturn_t`, `CURLcode`).
- **Concurrency:** Read [src/THREAD_SAFETY.md](src/THREAD_SAFETY.md) before adding locks or threads.
- **Logging:** `log_write` / `DEBUG_*` per [ERROR_HANDLING.md](ERROR_HANDLING.md); avoid `printf` for operational errors.
- **Asserts:** `assert()` is for programmer errors only; release builds use `-DNDEBUG`.
- **Scope:** Smallest correct diff; no unrelated reformatting.
- **Large files:** Do not split `socketio.c`, `ui_act.c`, `main.c`, `player.c`, or `ui.c` unless you are already changing that area for a real feature (see `.cursor/todo/06_file_function_size.plan.md`).

**Static analysis:** `make lint` uses cppcheck with [.cppcheck-suppress](.cppcheck-suppress). Fix new findings or add documented suppressions.

---

## Web UI (Lit + TypeScript)

- Stack: Lit 3, Vite, Vitest; i18n via `webui/src/i18n.ts` (`t`, `tf`) from generated JSON.
- `npm run prebuild` / `pretest` run `make -C .. locale-codegen` — still run codegen yourself after editing `locale/en.yaml`.
- Prefer unit tests for services and pure logic; Playwright E2E only when necessary (often needs a running server: `PIANOBAR_RUNNING=1`).
- Details: [webui/README.md](webui/README.md), [TESTING.md](TESTING.md).

---

## Localization

- Canonical English: [locale/en.yaml](locale/en.yaml).
- Run `make locale-codegen` (requires PyYAML).
- User-visible CLI, WebSocket errors, and web UI strings go through i18n — not raw English literals in code.
- Wire protocol / `eventcmd` field names stay English.
- Full rules: [.cursor/rules/localization-user-facing.mdc](.cursor/rules/localization-user-facing.mdc).

---

## Git and releases

- Pre-push hook enforces `make test-all`. Do not use `git push --no-verify` unless the user requests it.
- Immutable GitHub releases: create a **draft** release before CI uploads assets; publish after uploads complete (see [DEVELOPMENT.md](DEVELOPMENT.md)).

---

## Further reading

| Topic | Document |
|-------|----------|
| Dev environment | [DEVELOPMENT.md](DEVELOPMENT.md) |
| C tests | [README_TESTING.md](README_TESTING.md) |
| Web / E2E tests | [TESTING.md](TESTING.md) |
| WebSocket API | [WEBSOCKET_API.md](WEBSOCKET_API.md) |
| Configuration | [CONFIG.md](CONFIG.md) |
| Errors | [ERROR_HANDLING.md](ERROR_HANDLING.md) |
| Threading | [src/THREAD_SAFETY.md](src/THREAD_SAFETY.md) |
| CLI vs web UI | [CLI_vs_WEB.md](CLI_vs_WEB.md) |
