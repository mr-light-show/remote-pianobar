---
name: Step 4 – Magic numbers and conditional compilation
overview: Extract key timeouts, buffer sizes, and percentages into named constants; keep #ifdef as-is unless it grows, then consider platform.h.
todos:
  - id: const-websocket
    content: Add named constants in websocket code (e.g. WEBSOCKET_PING_TIMEOUT_SEC, VOLUME_FALLBACK_PERCENT)
  - id: const-others
    content: Extract other key literals (buffer sizes, timeouts) at top of file or in a header
  - id: ifdef-optional
    content: (Later) If #ifdef grows, consider platform.h with BAR_HAVE_ALSA, BAR_WEBSOCKET
isProject: false
---

# Step 4: Magic numbers and conditional compilation

**Findings:**

- Timeouts, buffer sizes, and percentages appear as raw literals (e.g. 25/60 sec in [src/websocket/core/websocket.c](src/websocket/core/websocket.c), 50% fallback in [src/websocket/protocol/socketio.c](src/websocket/protocol/socketio.c)).
- Many `#ifdef` blocks in [src/main.c](src/main.c) (`__APPLE__`, `WEBSOCKET_ENABLED`) and in [src/system_volume.c](src/system_volume.c), [src/player.c](src/player.c), [src/bar_state.c](src/bar_state.c).

**Recommendations:**

1. **Constants:** Extract key values (timeouts, buffer sizes, default percentages) into named constants in a header or at top of file (e.g. `WEBSOCKET_PING_TIMEOUT_SEC`, `VOLUME_FALLBACK_PERCENT`) to improve readability and change in one place.
2. **#ifdef:** Keep as-is for now; if it grows, consider a small platform/feature layer (e.g. `platform.h` with `BAR_HAVE_ALSA`, `BAR_WEBSOCKET`) so `#ifdef` is mostly in one place.
