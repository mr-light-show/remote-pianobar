---
name: Step 6 – File and function size (maintainability)
overview: Optional splits of large files (socketio.c, ui_act.c, main.c, player.c, ui.c) when touching those areas or adding features.
todos:
  - id: split-socketio
    content: (When touching) Split socketio.c by responsibility or event type; thin dispatcher
  - id: split-ui_act
    content: (When touching) Group ui_act by feature (station, song, playback, settings) into separate files
  - id: split-main
    content: (When touching) Extract startup/teardown and WebSocket init from main.c
  - id: split-player-ui
    content: (When adding features) Consider extracting filter graph and event loop from player.c / ui.c
isProject: false
---

# Step 6: File and function size (maintainability)

**Large files (by line count):**

| File                                                                   | Approx. lines | Note                                  |
| ---------------------------------------------------------------------- | ------------- | ------------------------------------- |
| [src/websocket/protocol/socketio.c](src/websocket/protocol/socketio.c) | ~1903         | Single large protocol handler         |
| [src/ui_act.c](src/ui_act.c)                                           | ~1257         | Many `BarUiAct`* callbacks            |
| [src/main.c](src/main.c)                                               | ~1016         | Entry, UI loop, WebSocket wiring       |
| [src/player.c](src/player.c)                                           | ~1035         | Decoder, filter chain, miniaudio glue |
| [src/ui.c](src/ui.c)                                                   | ~1075         | UI and event handling                 |

**Recommendations (optional, lower priority):**

- **socketio.c:** Split by responsibility: e.g. message parsing vs event handling vs JSON building, or by event type (playback, station, volume) into separate modules with a thin dispatcher.
- **ui_act.c:** Group actions by feature (station, song, playback, settings) into separate files (e.g. `ui_act_station.c`, `ui_act_playback.c`) and keep a single dispatch table or register from each.
- **main.c:** Extract "startup/teardown" and "WebSocket init" into separate files so `main()` is a short sequence of high-level steps.
- **player.c / ui.c:** Consider extracting "filter graph setup" and "event loop" into dedicated units if adding more features.

No need to split for splitting's sake; do it when touching the area or when adding features.
