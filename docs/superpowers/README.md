# Plans and specs

Design specs (`specs/`) say what to build and why; implementation plans (`plans/`) say how, task by task.

## Specs

| Spec | Status |
|---|---|
| [WebSocket simplification](specs/2026-11-12-websocket-simplification-design.md) | Implemented on `feature/ws-simplification` (#68–#85) |
| [v3.0.0 coordinated release and version-mismatch warnings](specs/2026-11-19-v3-release-and-version-checks-design.md) | Player side done (#83 S7); HA and card parts pending (HA plan Task 10, card Task 2c) |

## Plans

| Plan | Status |
|---|---|
| [CDN network logging](plans/2026-06-17-cdn-network-logging.md) | Earlier work |
| [Playback parked idle + wake on state change](plans/2026-06-17-playback-parked-idle.md) | Earlier work |
| [Account config validation](plans/2026-06-27-account-config-validation.md) | Earlier work |
| [Anti-pattern remediation (merged plan)](plans/antipattern-merge-sonet.md) | Earlier work |
| [WebSocket simplification](plans/2026-11-12-websocket-simplification.md) | Done: #68–#71, follow-ups #72–#85 |
| [macOS web-mode startup, `log_file` in every mode](plans/2026-11-18-macos-web-mode-startup.md) | Done: #80 |
| [Stalled-stream stop](plans/2026-11-19-stalled-stream-stop.md) | Done: #81 |
| [Linux LeakSanitizer leaks](plans/2026-11-19-linux-lsan-leaks.md) | Done: #85 (+ `c-asan` CI job) |
| [Build and test wall-clock reduction](plans/2026-11-19-build-time-reduction.md) | #79, #86, #87 done; Task 6 (CI image, Playwright cache) on `perf/ci-speed`, pending; Task 7 (ccache) skipped |

The Home Assistant side of v3.0.0 lives in the HA repo: `remote-pianobar-ha/docs/superpowers/plans/2026-11-18-ha-websocket-robustness.md` (runs after the player merges to `main`).
