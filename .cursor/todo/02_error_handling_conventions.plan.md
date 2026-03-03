---
name: Step 2 – Error handling conventions
overview: Document a single error-handling and goto convention; standardize goto label in system_volume.c; clarify assert vs release behavior.
todos:
  - id: err-doc
    content: Add error-handling convention (e.g. in DEVELOPMENT.md or ERROR_HANDLING.md) – cleanup for teardown, 0/success and error codes
  - id: err-goto
    content: Change system_volume.c goto label from error to cleanup to match rest of codebase
  - id: err-assert
    content: Document assert vs release (programmer error; release assumes they never fire) or replace critical external-input checks with explicit returns
isProject: false
---

# Step 2: Error handling conventions

**Current state:**

- **Return conventions:** Mixed: `0`/`1`, `true`/`false`, `-1`, `PianoReturn_t`, `CURLcode`. No project-wide standard.
- **Goto labels:** Inconsistent naming: `cleanup` in [src/ui.c](src/ui.c), [src/libpiano/request.c](src/libpiano/request.c), [src/libpiano/response.c](src/libpiano/response.c), [src/websocket/protocol/socketio.c](src/websocket/protocol/socketio.c) vs `error` in [src/system_volume.c](src/system_volume.c).
- **Asserts:** Used for "must not happen" (e.g. NULL, invariants) in [src/bar_state.c](src/bar_state.c), [src/ui_act.c](src/ui_act.c), [src/player.c](src/player.c), [src/main.c](src/main.c). With `NDEBUG`, these become no-ops and there is no documented runtime path for those cases in release builds.

**Recommendations:**

1. **Document a single convention** (e.g. in [DEVELOPMENT.md](DEVELOPMENT.md) or a short `ERROR_HANDLING.md`): use `cleanup` for multi-resource teardown; document that functions return 0 on success and a small set of error codes or bool where appropriate.
2. **Standardize goto label:** Use `cleanup` (or consistently `error`) in [src/system_volume.c](src/system_volume.c) to match the rest of the codebase.
3. **Assert vs release:** Either (a) add a short note in docs that asserts are "programmer error" and release builds assume they never fire, or (b) replace critical checks (e.g. NULL after external input) with explicit error returns and keep assert only for internal invariants.
