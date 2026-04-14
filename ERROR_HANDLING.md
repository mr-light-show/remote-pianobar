# Error Handling Conventions

Remote Pianobar uses a few different return styles depending on the subsystem, so the goal is consistency within a subsystem rather than forcing one universal error type.

## Return conventions

- `bool` for simple success/failure APIs.
- `0` / nonzero for C-style helper functions when the surrounding code already follows that convention.
- `-1` for read-only queries that naturally return a numeric value and need a sentinel failure value.
- `PianoReturn_t` for `libpiano` request/response paths.
- `CURLcode` for direct libcurl operations.

When adding new code, follow the pattern already used by the surrounding module.

## Cleanup labels

Use `cleanup` for multi-step teardown paths that free several resources before returning.

- Jump to `cleanup` when any owned resource needs to be released.
- Prefer a single cleanup block over duplicated error branches.
- Release any held locks before the jump or inside the cleanup block.

See [src/THREAD_SAFETY.md](src/THREAD_SAFETY.md) for the lock hierarchy and safe cleanup patterns.

## Asserts vs release builds

Assertions are for programmer errors and internal invariants.

- Keep `assert()` for cases that should never happen if the code is correct.
- Treat allocator failures and external-input-dependent checks as runtime errors instead of invariants.
- Remember that release builds compile with `-DNDEBUG`, so assertions disappear entirely.

The `curl_easy_escape()` null check in [src/libpiano/request.c](src/libpiano/request.c) is the kind of case that should use an explicit error return instead of an assertion.

## Logging

Use the logging API based on who needs to see the message:

- `log_write(LOG_ERROR, ...)` for errors that should always be visible.
- `log_write(DEBUG_*, ...)` for diagnostic output that should respect `PIANOBAR_DEBUG`.
- `BarUiMsg(..., MSG_ERR, ...)` or `MSG_NONE` for user-facing CLI messages.

The logging module in [src/log.c](src/log.c) timestamps output in `HAVE_DEBUGLOG` builds, prints a **kind label** (`Network`, `Audio`, `UI`, `WebSocket`, `WS_Progress`, `CLI`, or `Error`) after the timestamp in the same color as the timestamp, then the message body in the default color. **`DEBUG_CLI`** carries [`BarUiMsg`](src/ui.c) text on stderr when the internal debug mask includes that kind (which `log_init` ensures whenever any `PIANOBAR_DEBUG` bit is non-zero) and UI mode is web or both; in that case the same message is **not** also written to stdout. Downstream docs and tooling should expect ANSI escape sequences when stderr is captured from such builds.

## Common pattern

If a function opens files, allocates memory, or acquires locks, prefer this shape:

1. Validate inputs.
2. Allocate or open resources.
3. On failure, set an error result and jump to `cleanup`.
4. Release every owned resource in `cleanup`.
5. Return the final status code.

This keeps teardown predictable and makes error paths easier to audit.
