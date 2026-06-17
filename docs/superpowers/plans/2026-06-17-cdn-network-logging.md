# CDN Network Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit `DEBUG_NETWORK` request/response lines for Pandora CDN audio fetches (FFmpeg `avformat_open_input`), matching the existing Pandora API log shape in `BarPianoHttpRequest`.

**Architecture:** Add two small shared helpers in `log.c` (`log_network_request`, `log_network_response`) used by both `ui.c` (Pandora API) and `player.c` (CDN audio). Instrument `openStream()` in `player.c` to log `← URL` before open and `→ summary` after success or failure. No FFmpeg `av_log` hook and no custom `AVIOContext`—minimal surface area, same `PIANOBAR_DEBUG=1` gate as API traffic.

**Tech Stack:** C, FFmpeg (`libavformat`), existing `log_write(DEBUG_NETWORK, …)` infrastructure, Check unit tests.

**Out of scope:** Album-art CDN (not fetched by the C player), WebSocket HTTP static assets, logging binary audio bodies, redirect-chain logging inside FFmpeg.

---

## File map

| File | Responsibility |
|------|----------------|
| `src/log.h` / `src/log.c` | Shared `log_network_request` / `log_network_response` helpers |
| `src/ui.c` | Replace inline `DEBUG_NETWORK` strings with helpers (DRY) |
| `src/player.c` | Log CDN fetch in `openStream()` around `avformat_open_input` |
| `test/unit/test_log.c` | Unit tests for helpers (stderr capture) |
| `DEVELOPMENT.md` | One-line note that Network bit covers API + CDN audio |

---

## Log format (must match existing Network lines)

Pandora API today (`src/ui.c`):

```
[12:36:00.265] Network    : ← https://tuner.pandora.com:443/services/json/?method=...
[12:36:01.057] Network    : → {"stat":"ok",...}
```

CDN audio (new):

```
[12:36:00.100] Network    : ← http://audio-X.p-cdn.com/.../stream.mp3?...
[12:36:00.250] Network    : → ok
```

On failure (403 stale URL case):

```
[12:36:00.100] Network    : ← http://audio-X.p-cdn.com/.../stream.mp3?...
[12:36:00.264] Network    : → error: Server returned 403 Forbidden (access denied)
```

Rules:

- Request: full URL (same as API—signed CDN URLs are already exposed in playlist JSON / debug UI).
- Response success: short text only—never log audio bytes. Optional one-line summary after `find_stream_info` is acceptable (`→ ok (format=mp3)`) but not required for v1.
- Response failure: `→ error: <av_strerror message>`; keep existing CLI `Unable to open audio file (...)` unchanged.
- Only emit when `PIANOBAR_DEBUG` includes bit `1` (`DEBUG_NETWORK`); `log_write` already enforces this.

---

### Task 1: Shared network log helpers

**Files:**
- Modify: `src/log.h`
- Modify: `src/log.c`
- Modify: `src/ui.c`
- Test: `test/unit/test_log.c`

- [ ] **Step 1: Add declarations to `log.h`**

```c
/* DEBUG_NETWORK request/response lines (Pandora API + CDN audio). */
void log_network_request(const char *url);
void log_network_response(const char *summary);
```

- [ ] **Step 2: Implement in `log.c`**

```c
void log_network_request(const char *url)
{
	if (url == NULL) {
		return;
	}
	log_write(DEBUG_NETWORK, "← %s\n", url);
}

void log_network_response(const char *summary)
{
	if (summary == NULL) {
		return;
	}
	log_write(DEBUG_NETWORK, "→ %s\n", summary);
}
```

- [ ] **Step 3: Refactor `BarPianoHttpRequest` in `ui.c`**

Replace:

```c
log_write(DEBUG_NETWORK, "← %s\n", url);
/* ... curl ... */
log_write(DEBUG_NETWORK, "→ %s\n", req->responseData);
```

With:

```c
log_network_request(url);
/* ... curl ... */
log_network_response(req->responseData != NULL ? req->responseData : "(null)");
```

(`(null)` only if `responseData` can be NULL on success path—match current behavior: Pandora responses are always non-null strings when `CURLE_OK`.)

- [ ] **Step 4: Write failing tests in `test/unit/test_log.c`**

Add stderr pipe capture (extend existing `suppress_stderr` pattern or use `pipe` + `read`):

```c
START_TEST(test_log_network_request_and_response) {
	int pipefd[2];
	ck_assert(pipe(pipefd) == 0);
	ck_assert(dup2(pipefd[1], STDERR_FILENO) >= 0);
	close(pipefd[1]);

	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "1", 1), 0);
	log_init();
	log_network_request("http://cdn.example/track.mp3");
	log_network_response("ok");

	char buf[512] = {0};
	ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
	close(pipefd[0]);
	dup2(STDERR_FILENO, STDERR_FILENO); /* restore via saved dup if needed */
	unsetenv("PIANOBAR_DEBUG");

	ck_assert(n > 0);
	ck_assert(strstr(buf, "Network") != NULL);
	ck_assert(strstr(buf, "← http://cdn.example/track.mp3") != NULL);
	ck_assert(strstr(buf, "→ ok") != NULL);
}
END_TEST
```

Add `test_log_network_respects_mask` asserting **no** output when `PIANOBAR_DEBUG=2` (audio only).

- [ ] **Step 5: Run tests**

```bash
cd /Users/khawes/stash/pianobar/remote-pianobar
make test-all
```

Expected: new tests PASS; no regressions.

- [ ] **Step 6: Commit**

```bash
git add src/log.h src/log.c src/ui.c test/unit/test_log.c
git commit -m "refactor: shared DEBUG_NETWORK request/response helpers"
```

---

### Task 2: Instrument CDN fetch in `openStream()`

**Files:**
- Modify: `src/player.c`
- Test: manual verification (FFmpeg required; no mock in unit suite)

- [ ] **Step 1: Include `log.h` in `player.c`** (if not already present).

- [ ] **Step 2: Log before `avformat_open_input`**

In `openStream()`, immediately before the existing `avformat_open_input` call (~line 716):

```c
assert(player->url != NULL);
log_network_request(player->url);
if ((ret = avformat_open_input(&player->fctx, player->url, NULL, &options)) < 0) {
```

- [ ] **Step 3: Log failure inside the existing error branch**

Before `softfail("Unable to open audio file")`:

```c
char avmsg[AV_ERROR_MAX_STRING_SIZE];
av_strerror(ret, avmsg, sizeof avmsg);
log_network_response(avmsg[0] != '\0'
	? (snprintf((char[128]){0}, 128, "error: %s", avmsg), /* use a local errbuf instead */ )
	: "error: unknown");
```

Use a clean local buffer:

```c
char errSummary[160];
snprintf(errSummary, sizeof errSummary, "error: %s",
	avmsg[0] != '\0' ? avmsg : "unknown");
log_network_response(errSummary);
```

- [ ] **Step 4: Log success after open succeeds**

After `av_dict_free(&options)` on the success path (still inside `openStream`, before `find_stream_info`):

```c
log_network_response("ok");
```

Do **not** log again after `find_stream_info` unless you add format detail in a follow-up—one request/response pair per `openStream` call.

- [ ] **Step 5: Manual verification**

```bash
PIANOBAR_DEBUG=1 ./pianobar   # or websocket build
# Play a station; expect paired Network lines for CDN URL before Audio decode logs.
# Force stale URL (wait >4h or use expired playlist) → expect → error: ... 403 ...
```

- [ ] **Step 6: Run full test suite**

```bash
make test-all
```

Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/player.c
git commit -m "feat: log CDN audio fetches as DEBUG_NETWORK"
```

---

### Task 3: Documentation

**Files:**
- Modify: `DEVELOPMENT.md` (Network debug bit section)

- [ ] **Step 1: Update `PIANOBAR_DEBUG` bit `1` description**

Add after the Network bullet:

> Network (`1`) — Pandora API calls (`tuner.pandora.com` via curl) **and** CDN audio stream opens (FFmpeg `avformat_open_input` on track `audioUrl`).

- [ ] **Step 2: Commit**

```bash
git add DEVELOPMENT.md
git commit -m "docs: DEBUG_NETWORK includes CDN audio fetches"
```

---

## Self-review (spec coverage)

| Requirement | Task |
|-------------|------|
| CDN fetches visible as Network logs | Task 2 |
| Same `PIANOBAR_DEBUG=1` gate | Tasks 1–2 via `log_write(DEBUG_NETWORK)` |
| Match `←` / `→` shape | Task 1 helpers |
| No binary body logging | Task 2 (`→ ok` / `→ error: …` only) |
| Preserve CLI error on 403 | Task 2 (softfail unchanged) |
| Tests | Task 1 |
| Docs | Task 3 |

No placeholders; all paths and signatures defined.

---

## Risks and decisions

1. **Duplicate lines with CLI on failure** — Acceptable: Network shows transport result; CLI shows user-facing `Unable to open audio file (...)`. Same pattern as API errors (`Network error` CLI + Network `→` body).
2. **FFmpeg redirects** — Only the URL passed to `avformat_open_input` is logged; redirect targets stay invisible unless we later add an `av_log` callback (not planned).
3. **Thread safety** — `openStream` runs on the player thread; `log_write` is already used from multiple threads for Network/API—no new mutex required.
4. **Future: album art** — If the card/web UI fetches `coverArt` outside this process, that remains out of scope; document if a separate HTTP client is added later.
