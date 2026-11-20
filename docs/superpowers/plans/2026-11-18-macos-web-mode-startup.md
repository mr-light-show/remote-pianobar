# macOS `ui_mode = web` Startup and `log_file` in Every Mode: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Web-only daemon startup leaves fds 0/1/2 valid (stdin on `/dev/null`) and shows the real fatal startup error instead of only the misleading `lws: ZERO RANDOM FD`. Also make `log_file` work in `both` and `cli` modes (Task 4), which today ignore it.

**When:** After the stack merges, as one "logging" PR into `feature/ws-simplification`.

**Architecture:** Replace the bare `close(STDIN_FILENO)` in `BarDaemonizeSteps` with a small, unit-testable stdio-redirect helper that keeps fds 0/1/2 open on `/dev/null` or the log file. Separately, send `MSG_ERR` to stderr when the web-only daemon runs without `log_file` (stdout is `/dev/null` in that case).

**Tech Stack:** C (GNU style), Check unit tests, libwebsockets 4.3.3 (Ubuntu CI) / 4.4.1 (Homebrew).

**Spec:** This document; the investigation findings are below.

---

## Investigation findings (read first)

### History: why stdin is closed instead of redirected

| Commit | Date | What happened |
|---|---|---|
| `284212a` "Implement daemon mode for ui_mode=web" (pre-squash, not an ancestor of `main`) | 2025-11-25 | Added `BarDaemonize`: fork, `setsid`, `chdir("/")`, **`close(STDIN_FILENO)`**, stdout+stderr to `log_file` or `/dev/null`. |
| `efa96d8` "Fix daemon mode crash by skipping terminal/stdin operations" (pre-squash) | 2025-11-25 | **This is the earlier issue in this area.** The daemon crashed because, after stdin was closed, `main.c` still ran `BarTermInit` (`tcgetattr` on fd 0), put fd 0 into the `select()` set, and called `BarMainHandleUserInput`. The fix gated all of that on the mode instead of reopening stdin: no `BarTermInit`, `fds[0]=fds[1]=-1`, `maxfd=0` (now `BarWsConfigureWebOnlyInput`), no user input handling. |
| `48f628c` "Add WebSocket server and web UI" | 2025-11-26 | Squashed import of the above into `main`. `close(STDIN_FILENO)` has stayed unchanged since then. |
| `1f68282` "macOS web mode: Add fork+exec relaunch for CoreAudio compatibility" | 2026-01-08 | macOS re-execs itself with `--launched-as-daemon` and calls `BarDaemonizeSteps` (no second fork). Same stdin handling on every platform. |
| `e257a3f` "Add journal-friendly debug logging" | 2026-07-27 | With no `log_file`, stderr is now **kept** for the systemd journal; only stdout goes to `/dev/null`. Test `test_daemonize_steps_without_log_file_keeps_stderr` (`0644192`). |
| `e0cb389`/`39d6dd8`/… "readline: return immediately when there are no input fds" (unmerged, ws-simplification branches) | 2026-11-17 | Depends on web mode keeping `maxfd <= 0`. Not affected by this plan. |

So `close()` was never a deliberate fix. The fix that matters is `efa96d8`: **web mode must not use fd 0 as terminal or input.** That check is based on the mode, not on whether fd 0 is valid, so putting `/dev/null` on fd 0 keeps it intact. No commit, doc (`CONFIG.md`, `DEVELOPMENT.md`, `ChangeLog`, `docs/`) or platform note (launchd, HA add-on, SIGTTIN) depends on fd 0 being closed.

### Reproduction (scratch worktree from `origin/main` f28effe, isolated HOME/XDG, port 47913, fake credentials)

- **macOS, Homebrew lws 4.4.1:** reproduced `[..] Error : lws: ZERO RANDOM FD` on stderr.
- Instrumented fd dump: fd 0 is **CLOSED** after `BarDaemonizeSteps`, after `BarPlayerInit`, after `PianoInit` and just before `lws_create_context`. So lws's `open("/dev/urandom")` gets fd 0.
- With `PIANOBAR_DEBUG=65535` the full sequence is: `lws_create_context` OK → `Server started on port 47913` (port listening, confirmed with lsof) → login → `Error: Wrong email address or password.` → `Stopping server...` → **`lws: ZERO RANDOM FD`** → `Server stopped`.
- **Root cause of the message:** in both lws 4.3.3 and 4.4.1, `ZERO RANDOM FD` is a **non-fatal `lwsl_err` in `lws_plat_context_late_destroy()`** (`lib/plat/unix/unix-init.c`). It runs when the context is *destroyed* and `fd_random == 0`. lws does not reject fd 0 at init. The earlier claim that "lws rejects fd 0" is wrong.
- **Why it looks like the cause of death:** with no `log_file`, stdout is `/dev/null` and `BarUiMsg(MSG_ERR, …)` writes to stdout. The actual fatal error (here the login failure) is discarded. The only `LOG_ERROR` line that reaches stderr is the teardown message. Observed stderr (no log_file): banner lines, then only `lws: ZERO RANDOM FD`.
- **Scratch patch (`/dev/null` dup2'd onto fd 0):** fd 0 = `/dev/null`, the server listens, the same login failure shuts it down, and the message is gone. The patch removes the misleading message. It does **not** change why the process exits.
- **Linux (`ubuntu:24.04` container, lws 4.3.3, `PIANOBAR_TEST_NO_DEVICE=1`, same Linux `BarDaemonize` fork path):** identical results. fd 0 is CLOSED before lws, and `lws: ZERO RANDOM FD` appears at teardown. Linux is not immune. On real hardware, an ALSA/Pulse fd opened during `BarPlayerInit` may land on fd 0 and hide the message. In that case the audio fd becomes "stdin" for every child (eventcmd before `dup2`, `popen("pactl …")`).

### Conclusion

1. The bug is real: closed fd 0 is reused by whatever opens next. It is cosmetic for lws (misleading teardown error) and a hygiene hazard for children. Fix: keep fd 0 on `/dev/null`.
2. The user's real "dies at startup" cause is **still unknown**. It is whatever triggered shutdown, and its message went to `/dev/null`. Task 2 makes it visible. The user should rerun with `log_file = /tmp/pianobar.log` (or `PIANOBAR_DEBUG=65535`) to see it now.

## Fix options considered

| Option | Pros | Cons |
|---|---|---|
| **A. Redirect stdin to `/dev/null` (`open` + `dup2`), and make 1/2 always end up valid (recommended)** | Standard daemon idiom. Keeps `efa96d8` behaviour (mode gates unchanged) and `e257a3f` (stderr kept with no log_file). Fixes both platforms. Also fixes the latent bug where the `log_file` `open()` returns fd 0 and is then closed. | Touches every web-mode start, so needs a unit test on the fd table. |
| B. Leave `close(0)`, initialise lws before daemonize | No daemon change | Breaks the documented ordering (lws thread/ports before fork; `1f68282`/`4e802ba` require init after fork on macOS). Does not fix child-inheritance hazards. Rejected. |
| C. Ignore the message (filter `ZERO RANDOM FD` in `BarWsLwsLogEmit`) | Trivial | Hides a symptom. fd 0 is still reused. Rejected. |
| D. Only fix error visibility (Task 2) | Shows the real error | fd 0 stays dangling. Do it **in addition to** A, not instead. |

**Recommendation:** A (Task 1) + D (Task 2), in that order. Task 3 adds a startup smoke test.

## Global Constraints

- Only for `ui_mode = web`. Do not change `cli`/`both` startup paths.
- Do not change `main.c` mode gates from `efa96d8`: web mode must not `BarTermInit`, must not put any fd in `app.input.set`, and must keep `maxfd = 0` (`BarWsConfigureWebOnlyInput`).
- Without `log_file`: stdout → `/dev/null`, **stderr unchanged** (journal), per `e257a3f`.
- With `log_file`: stdout and stderr → log file (append, 0644).
- GNU C style, `Bar*` names, `log_write` for errors; user-facing text through `locale/en.yaml` + `make locale-codegen` (no new strings expected).
- ≥90% patch coverage on `src/**` lines changed (`PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage`); `make test-all` before any push.

## Review Focus

1. Process started with fds 0/1/2 already closed (launchd/`nohup`/`pianobar <&- >&- 2>&-`): `open("/dev/null")` returns 0, 1 or 2. The helper must not close a descriptor it just placed on 0/1/2. Expect 0/1/2 all valid afterwards. Test: `test_daemon_redirect_stdio_with_std_fds_closed`.
2. `log_file` set but unopenable (bad dir/permissions): expect stdout on `/dev/null`, stderr kept, fd 0 still `/dev/null`, steps still succeed. Test: `test_daemon_redirect_stdio_bad_log_file_keeps_stderr`.
3. `/dev/null` unavailable (sandbox/chroot): expect the function to return false without closing fd 0 and to log via `log_write`. `BarDaemonizeSteps` still continues (no worse than today). Not unit-tested, so note it in review.
4. eventcmd child after daemonize: `dup2(pipeFd[0], 0)` must still give the child the pipe on stdin (fd 0 now open on `/dev/null`, so `pipe()` returns ≥3). Covered by existing eventcmd tests; check they still pass.
5. Web mode must still never read from fd 0: with fd 0 on `/dev/null`, `select()` would always report readable/EOF if any code path added fd 0 back. Test: `test_ws_configure_web_only_input_excludes_stdin` (existing coverage may already assert this; extend it if not).

---

### Task 1: Keep fds 0/1/2 valid in `BarDaemonizeSteps`

**Files:**
- Modify: `src/websocket/daemon/daemon.c` (`BarDaemonizeSteps`, ~lines 75–97 on `origin/main`)
- Modify: `src/websocket/daemon/daemon.h`
- Test: `test/unit/test_daemon.c` (register in `daemon_suite` tcase list ~line 196)

**Interfaces:**
- Produces: `bool BarDaemonRedirectStdio (const char *logFile);` declared in `daemon.h`. It opens `/dev/null` `O_RDWR` and `dup2`s it onto `STDIN_FILENO`. If `logFile != NULL` and it opens (`O_WRONLY|O_CREAT|O_APPEND, 0644`), it `dup2`s that onto 1 and 2. Otherwise it `dup2`s `/dev/null` onto 1 and leaves 2 alone. It closes temporary fds only when they are `> STDERR_FILENO`. Returns false only if `/dev/null` cannot be opened (logs `LOG_ERROR`, leaves fds as they were).
- `BarDaemonizeSteps` calls it in place of `close(STDIN_FILENO)` and the redirect block. A false return is logged but is not fatal.

- [ ] **Step 1: Write the failing tests.** Each test forks (the pattern from `test_daemonize_steps_without_log_file_keeps_stderr`), runs the code in the child, checks with `fstat`, and reports through `_exit` codes:
  - `test_daemonize_steps_reopens_stdin_on_dev_null`: child calls `BarDaemonizeSteps` (pidFile in `/tmp`, logFile NULL). Assert `fcntl(0, F_GETFD) != -1` and `fstat(0)` is `S_ISCHR` with the same `st_rdev` as `stat("/dev/null")`.
  - `test_daemonize_steps_with_log_file_keeps_stdin_open`: logFile = `/tmp/test_pianobar_daemon_steps.log`. Assert fd 0 = `/dev/null`, fd 1 and 2 have the same `st_ino` as the log file, and `write(1,…)` lands in the file. This is the case where today's code closes fd 0 twice.
  - `test_daemon_redirect_stdio_with_std_fds_closed`: child does `close(0); close(1)`, then `BarDaemonRedirectStdio(NULL)`. Assert 0 and 1 are `/dev/null` and 2 is unchanged (compare `st_ino`/`st_dev` from before).
  - `test_daemon_redirect_stdio_bad_log_file_keeps_stderr`: logFile = `/nonexistent-dir/x.log`. Assert it returns true, 0 and 1 are `/dev/null`, 2 is unchanged.
- [ ] **Step 2: Run to verify failure.** `make test` (or `CK_RUN_SUITE=daemon ./pianobar_test`). Expected: the first two fail (fd 0 closed) and the last two fail to link (`BarDaemonRedirectStdio` undefined).
- [ ] **Step 3: Implement `BarDaemonRedirectStdio`** in `daemon.c` and call it from `BarDaemonizeSteps`. Update the comment `/* Close standard file descriptors */` to explain why fd 0 must stay open (closed fd 0 is reused by e.g. lws's `/dev/urandom`; see this plan).
- [ ] **Step 4: Verify.** `make test` passes the daemon suite and all others. Then `make test-all` (cppcheck clean).
- [ ] **Step 5: Manual macOS check (scratch worktree, isolated `HOME`/`XDG_CONFIG_HOME`, `mkdir -p $HOME/.config/pianobar` first so the lock check never looks at the real lock, free port, fake credentials):** stderr must no longer show `lws: ZERO RANDOM FD`, and `lsof -p <pid>` must show fd 0 = `/dev/null`.
- [ ] **Step 6: Commit** `fix(daemon): keep stdin on /dev/null instead of closing it`.

### Task 2: Show fatal `MSG_ERR` on stderr for web-only daemon without `log_file`

**Files:**
- Modify: `src/ui.c` (`BarUiMsg`, ~lines 129–158)
- Test: `test/unit/test_ui*.c` (whichever suite already covers `BarUiMsg`; add a case if none)

**Interfaces:**
- Consumes: `BarSettings_t.uiMode`, `BarSettings_t.logFile`.
- Behaviour: when `settings->uiMode == BAR_UI_MODE_WEB && settings->logFile == NULL && type == MSG_ERR`, write prefix/body/postfix to **stderr** (no `\033[2K`) instead of stdout. Everything else is unchanged (no journal spam from `MSG_TIME`/`MSG_INFO`). The `HAVE_DEBUGLOG` early path (already `log_write`) is unchanged.

- [ ] **Step 1: Failing test** `test_ui_msg_err_goes_to_stderr_in_web_only_without_log_file`: redirect fd 2 to a temp file (`dup`/`dup2`), call `BarUiMsg(&settings, MSG_ERR, "boom\n")` with `uiMode = BAR_UI_MODE_WEB` and `logFile = NULL`, restore, and assert the file contains `boom`. Companion `test_ui_msg_info_stays_on_stdout_in_web_only`: `MSG_INFO` text not in the stderr capture. Companion `test_ui_msg_err_stays_on_stdout_with_log_file`: logFile non-NULL means nothing on stderr.
- [ ] **Step 2: Run, expect FAIL** (`boom` not on stderr).
- [ ] **Step 3: Implement** by picking `FILE *out` at the top of the non-debug path and using it for `fputs`/`vfprintf`/`fflush`.
- [ ] **Step 4: Run `make test`, expect PASS.** Manual: the fake-credential repro now prints `Error: Wrong email address or password.` on stderr.
- [ ] **Step 5: Commit** `fix(ui): surface fatal errors on stderr in web-only daemon without log_file`.

### Task 3: Web-mode startup smoke test

**Files:**
- Create: `test/integration/web_mode_startup.sh` (or add a case to the existing integration runner if one exists; follow `README_TESTING.md`)
- Modify: `Makefile` only if the integration runner needs registration (then follow `.agents/skills/pre-push-ci-parity/SKILL.md`)

- [ ] **Step 1: Script:**
  - Create a temp dir T with `HOME=$T/home` (with `.config/pianobar/`) and `XDG_CONFIG_HOME=$T/xdg`.
  - Write a config with `ui_mode = web`, `websocket_host = 127.0.0.1`, a free high port, `pid_file`, and **no** `log_file`.
  - Set `PIANOBAR_TEST_NO_DEVICE=1`.
  - Start `./pianobar 2>$T/err`.
  - Wait until the port listens or the PID exits (≤15 s).
  - Send SIGTERM and wait for exit.
  - Assert `$T/err` does **not** contain `ZERO RANDOM FD`.
  - On Linux, also assert `readlink /proc/$PID/fd/0` = `/dev/null` while the process runs.
  - Only use Pandora login when `PIANOBAR_INTEGRATION=1`. Without network the login fails, and then the test must find the login error text on stderr (Task 2).
- [ ] **Step 2: Run on `main` without Tasks 1–2, expect FAIL.**
- [ ] **Step 3: Run with Tasks 1–2, expect PASS** locally (macOS) and in `make test-ci-local`.
- [ ] **Step 4: Commit** `test: smoke-test web-only daemon startup fd hygiene`.

### Task 4: Honor `log_file` in `both` and `cli` modes (added 2026-11-19)

**Finding:** `log_file` is read in exactly one place, `BarDaemonizeSteps` (`src/websocket/daemon/daemon.c:80-93`), which points stdout/stderr at it when the web-only process detaches. In `both` and `cli` the process never detaches, so the setting is ignored: `log_write` writes debug lines and `LOG_ERROR` to stderr only (`src/log.c:103-160,194-211`), i.e. the terminal. `CONFIG.md:273` documents it as "Path to log file for daemon mode". `build.sh:292-305` still rotates `log_file` and prints "Rotating log file" in every mode, which is misleading. An uncommitted `build.sh` change (wrap pianobar in `script(1)` to record the terminal) worked around this; it is superseded by this task and its patch (`build-sh-session-log.patch` in the repo root) should be deleted.

**Decided behavior:**

| Mode | `log_file` set | Debug lines + `LOG_ERROR` | UI text (`BarUiMsg`, prompts) |
|---|---|---|---|
| `web` | yes | log file (unchanged, via stderr redirect) | log file (unchanged) |
| `web` | no | stderr (unchanged) | `/dev/null`, except `MSG_ERR` (Task 2) |
| `both` / `cli` | yes | **terminal and log file** (new) | terminal only (unchanged) |
| `both` / `cli` | no | terminal (unchanged) | terminal (unchanged) |

The log file gets plain text (no ANSI colour codes) with the same `[hh:mm:ss.mmm] KIND : ` prefix. Terminal output is unchanged.

**Files:**
- Modify: `src/log.c`, `src/log.h` (a `log_set_file(const char *path)` / `log_close_file()` pair; a mutex-free `FILE *` written with one `fprintf` per line is acceptable if each line is formatted into a buffer first and written with a single `fwrite`, so lines from different threads don't interleave)
- Modify: `src/main.c` (after settings load and before the first `log_write` that matters: if `log_file` is set and the mode is not web-only, call `log_set_file`; close at exit)
- Modify: `CONFIG.md` (`log_file` row: "Log file. In `web` mode all output goes here; in `both`/`cli` debug and error log lines are also written here")
- Modify: `build.sh` (rotation message only when the file exists, wording "Rotating log file" kept; no `script(1)`)
- Test: `test/unit/test_log.c` (create if missing; register in `test/test_main.c` and `TEST_SRC`)

- [ ] **Step 1: Failing tests**
  - `test_log_file_receives_error_lines`: `log_set_file(tmp)`, `log_write(LOG_ERROR, "boom %d\n", 1)`, close; file contains `ERROR` prefix and `boom 1`, and no `\033[`.
  - `test_log_file_receives_enabled_debug_lines_only`: debug mask with `DEBUG_UI` only; a `DEBUG_UI` line is in the file, a `DEBUG_NETWORK` line is not.
  - `test_log_file_terminal_output_unchanged`: capture fd 2 (`dup`/`dup2`) while writing one line; stderr still gets the coloured line.
  - `test_log_file_unwritable_path_is_ignored`: `log_set_file("/nonexistent/x.log")` returns false, logs one `LOG_ERROR` to stderr, and later `log_write` calls still work.
  - `test_log_file_concurrent_writers_do_not_interleave`: 4 threads × 200 lines; every line in the file is intact.
  - `test_main_sets_log_file_in_both_mode_only` (`test_player.c`/main-include pattern): `both` → file opened; `web` → not opened by this path (daemon redirect still handles it).
- [ ] **Step 2: Run, expect FAIL** (`log_set_file` undefined).
- [ ] **Step 3: Implement.** Append mode (`O_APPEND`), line-buffered. Keep `HAVE_DEBUGLOG` behavior: in minimal builds only `LOG_ERROR` exists, and it goes to the file too.
- [ ] **Step 4: Verify.** `make test-all`, `make test-tsan` (0 warnings, the concurrent-writer test runs under it), `make test-asan`, ≥90% patch coverage, `make test-ci-local`.
- [ ] **Step 5: Manual.** `./build.sh` with `ui_mode = both`, `log_file = /tmp/pianobar.log`, `PIANOBAR_DEBUG=15`: the terminal looks as before, and `/tmp/pianobar.log` gets the same log lines without colour codes.
- [ ] **Step 6: Commit** `feat(log): write log lines to log_file in both and cli modes`, and delete `build-sh-session-log.patch`.

**Acceptance:** Setting `log_file` captures debug and error log lines in every UI mode; terminal output in `both`/`cli` is unchanged; `web` mode is unchanged.

## Open questions

- What exactly did the user see before `ZERO RANDOM FD`? Their real shutdown reason is hidden. Ask them to rerun with `log_file` or `PIANOBAR_DEBUG=65535` (no code change needed) to tell a login/network failure from a different bug.
- Should Task 2 go further and route all `MSG_ERR` through `log_write(LOG_ERROR, …)` in web mode, so it gets timestamp/colour like other journal lines? That is a product decision. This plan keeps the existing format.
- Task 4: should UI text (`BarUiMsg` info lines such as "Login...", song changes) also be copied to `log_file` in `both`/`cli`, or only log lines? Default in this plan: only log lines; the terminal already shows UI text, and copying it would need `BarUiMsg` changes.
- Real Linux hardware (ALSA/Pulse): which fd lands on 0 today? Check with `ls -l /proc/<pid>/fd` on the HA/Pi host. Task 1 makes this moot.
