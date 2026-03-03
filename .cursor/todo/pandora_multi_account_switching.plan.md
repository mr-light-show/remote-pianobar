---
name: Pandora multi-account switching
overview: Add support for multiple Pandora accounts in remote-pianobar, with config-file options, and expose account selection in the web UI and Home Assistant via a select control.
todos: []
isProject: false
---

# Pandora multi-account credential switching

## Scope

- **remote-pianobar**: Config format for multiple accounts, runtime account list + active index, reconnect with optional account selection, and WebSocket state for UIs.
- **Web UI**: Account dropdown; send `app.pandora-reconnect` with chosen account when switching.
- **remote-pianobar-ha**: New Select entity for "Account to use"; optional `account_id` on reconnect service.

Single-process model: one daemon, one active Pandora session at a time; switching account = disconnect (if needed) then reconnect with the selected account's credentials.

---

## Config file format (include-file per account)

Current config is key-value in [settings.c](remote-pianobar/src/settings.c) (`key = value`). Two modes:

**One account:** Credentials go in the main config. No `account` lines. `user` and `password` (or `password_command`) live in the main config file. Behaves as today (single account, index 0).

**Multiple accounts:** All credentials live in separate files. Main config lists accounts with `account = id:path` and contains only global settings (network, audio, WebSocket, keybindings, etc.). **If both credentials (`user`/`password`) exist in the main config and `account = id:path` lines exist, ignore the credentials in the main file and print a warning to stdout on startup** (so users are not silently wrong). Each account's credentials and optional per-account options (auth, Interface/Display, Sort, Station Display Name Override, Format Strings, Message Formats, `account_label`) come from that account's file. Effective config for an account = merge(main config, account file); main supplies defaults, account file overrides; in multi-account mode do not use main-config user/password. Account used at startup: if `default_account` is set in the main config (must match an account id), use that account; otherwise use the first listed `account` (index 0).

- **Path resolution** for `account = id:path`: If `path` starts with `~` or `/`, use it as-is (after tilde expansion). Otherwise resolve relative to the main config file's directory. Examples: `work:work.conf`, `personal:~/.config/pianobar/accounts/personal.conf`, `other:/usr/home/fred/.config/pianobar/fred.conf`.
- **Account display name**: Optional `account_label` in each account's file. If not specified, use the id from the `account = id:path` line (e.g. `other`).

**Single-account example** (main config only):

```ini
user = me@example.com
password = secret
sort = name_az
websocket_port = 9001
```

**Multi-account example** (main config — no user/password):

```ini
# Global settings only; if user/password appear here with account lines, they are ignored and a warning is printed
sort = name_az
websocket_port = 9001
# default_account = work   # optional; if unset, first listed account is used at startup

account = work:work.conf
account = personal:~/.config/pianobar/accounts/personal.conf
account = other:/usr/home/fred/.config/pianobar/fred.conf
```

Example account file `work.conf` (credentials + optional overrides):

```ini
user = work@example.com
password = workpass
account_label = Work
autostart_station = 98765
```

(If `account_label` is omitted, display name is the id from the account line, e.g. `work`.)

---

## Backend (remote-pianobar) implementation

### 1. Data structures

- **Account entry**: `typedef struct { char *id; char *label; char *username; char *password; char *passwordCmd; } BarAccount_t;` (id from config, e.g. "work"; label = `account_label` from that account's file if present, else id).
- **Settings**: In [settings.h](remote-pianobar/src/settings.h), add to `BarSettings_t`:  
`BarAccount_t *accounts; size_t accountCount; size_t activeAccountIndex;`  
Single-account mode: no `account` lines; main config has user/password; one account (index 0) built from main config. Multi-account mode: main config has `account = id:path` only (no user/password); each account's effective config = merge(main, account file). Keep existing `username`/`password` for backward compatibility in single-account mode.
- **Helpers**: `BarSettingsGetActiveAccount()`, `BarSettingsSetActiveAccount(index or id)`; resolve `password_command` per account when building login credentials (reuse existing passwordCmd logic).

### 2. Config parsing

- In [settings.c](remote-pianobar/src/settings.c) `BarSettingsRead`:
  - **Single-account**: If no `account = id:path` lines, read main config as today; build one account (index 0) from main config. Label = `account_label` if present in main config, else a default (e.g. id `"0"` or `"main"`).
  - **Multi-account**: If any `account = id:path` line exists, treat as multi-account. If the main config also contains `user` or `password`, ignore those credentials (do not use them for any account) and **print a warning to stdout on startup** (e.g. "Warning: account files are specified; ignoring user/password in main config."). Parse `account = id:path`; for each, resolve path and append an account entry (id + path). Parse optional `default_account = id`; if set, set activeAccountIndex to the index of that id (error if id not found); if not set, activeAccountIndex = 0. For each account, label = `account_label` from merged result if present, else the account id.
  - **Merge helper**: Add `BarSettingsMergeFromFile(BarSettings_t *base, const char *path)`: read the file at `path`; for each key present, override the corresponding field in `base`. For multi-account, effective config for an account = merge(main config, that account's file). For single-account, no merge (main config is the only account).
  - In `BarSettingsDestroy`, free each account's strings and the array (including account file paths).

### 3. Login and reconnect

- **Startup**: In [main.c](remote-pianobar/src/main.c) `BarMainLoginUser`, use credentials from `BarSettingsGetActiveAccount(app->settings)` (or fallback to `app->settings.username`/`password` if no accounts).
- **Reconnect**: In [ui_act.c](remote-pianobar/src/ui_act.c) `BarUiActPandoraReconnect`:
  - If WebSocket calls with payload, add a new path: parse `account_id` or `account` (index/label) from `data` (need to pass `json_object *data` from socketio into the callback or have a small helper that sets active account from JSON then calls existing reconnect logic). So either:
    - Extend the action dispatcher so that for `BAR_KS_PANDORA_RECONNECT` it can pass `data` into a new `BarUiActPandoraReconnectWithPayload(app, data)` that reads `account_id`/`account`, sets active account, then runs the same login + get-stations + resume flow; or
    - In `BarSocketIoHandleAction`, when action is `app.pandora-reconnect` and `data` is present, extract account, set `app->settings.activeAccountIndex`, then call the same `BarUiActPandoraReconnect` (which uses active account). Ensure thread safety (Piano/login is done on main loop).
  - If currently logged in and the selected account differs from the one that was used for the current session, perform disconnect (stop, clear stations, PianoDestroy) then reconnect with the new account's credentials.
- **Credentials for login**: When building `PianoRequestDataLogin_t`, use the active account's username/password (or run password_command for that account). Reuse existing password_command pattern from [main.c](remote-pianobar/src/main.c) if needed at runtime for an account.

### 4. WebSocket API

- **Reconnect with account**: In [socketio.c](remote-pianobar/src/websocket/protocol/socketio.c), when handling `action` `app.pandora-reconnect` with object `data`, read `account_id` or `account` (string: index or label). Set active account; then call disconnect (if connected) + existing reconnect flow. Document in [WEBSOCKET_API.md](remote-pianobar/WEBSOCKET_API.md): e.g. `2["action",{"action":"app.pandora-reconnect","account_id":"1"}]`.
- **State**: In `BarSocketIoEmitProcess` ([socketio.c](remote-pianobar/src/websocket/protocol/socketio.c) ~568), add to the emitted JSON:
  - `current_account`: object `{ "id": "0", "label": "Work" }` (id = stable id, label = display name).
  - `accounts`: array of `{ "id": "0", "label": "Work" }` (no passwords). So clients get the list and the active one for a dropdown.

Emit the same state after reconnect or account switch (already done via existing `BarSocketIoEmitProcess` / broadcast after reconnect).

---

## Web UI (remote-pianobar webui)

- **State**: In [webui/src/app.ts](remote-pianobar/webui/src/app.ts), from the `process` (or equivalent) payload, store `current_account` and `accounts` in component state.
- **UI**: Add an account selector (e.g. in the top bar or info menu): dropdown or list bound to `accounts`; selected = `current_account`. Only show when `accounts.length > 1`.
- **On change**: Send `2["action",{"action":"app.pandora-reconnect","account_id":"<selected id>"}]` (use existing SocketService). Optionally show a short "Switching account…" state and handle errors from `app.pandora-reconnect` (e.g. error event).
- **Reconnect button**: Keep as "reconnect with current account"; no change unless you want an optional "Reconnect as…" that opens a small modal to pick account then reconnect with that `account_id`.

---

## Home Assistant (remote-pianobar-ha)

- **Coordinator**: In [coordinator.py](remote-pianobar-ha/custom_components/pianobar/coordinator.py), when processing the WebSocket state that includes `process` (or the event that carries the same payload), update `coordinator.data` with `current_account` and `accounts` (list of `{ "id", "label" }`). Ensure this is the same structure the backend sends.
- **Select entity**: Add a second Select in [select.py](remote-pianobar-ha/custom_components/pianobar/select.py) (existing one remains the station selector):
  - Entity: e.g. "Pandora account" or "Account to use"; `options` = list of account labels (or ids); `current_option` = label (or id) of `current_account`.
  - `async_select_option`: map selected label back to account id, then call `await self.coordinator.send_action_with_params("app.pandora-reconnect", {"account_id": id})`. Coordinator already has [send_action_with_params](remote-pianobar-ha/custom_components/pianobar/coordinator.py) that sends `{"action": action, ...params}`.
- **Reconnect service**: Optionally extend the `reconnect` service in **[init**.py](remote-pianobar-ha/custom_components/pianobar/__init__.py) and [services.yaml](remote-pianobar-ha/custom_components/pianobar/services.yaml) to accept an optional `account_id` and pass it through `send_action_with_params("app.pandora-reconnect", {"account_id": account_id})` when provided.
- **Device/entity naming**: Place the new Select on the same device as the existing station Select; unique_id e.g. `{entry_id}_account_select`.

---

## Data flow (high level)

```mermaid
sequenceDiagram
  participant Config
  participant Daemon
  participant WebSocket
  participant WebUI
  participant HA

  Config->>Daemon: BarSettingsRead
  Daemon->>Daemon: Login with active account
  WebUI->>WebSocket: connect and query
  WebSocket->>WebUI: process state with stations and accounts
  HA->>WebSocket: connect and receive state
  WebUI->>WebSocket: pandora-reconnect with account_id
  WebSocket->>Daemon: set active account and reconnect
  Daemon->>WebSocket: process state updated
  WebSocket->>WebUI: broadcast updated state
  WebSocket->>HA: same state
  HA->>WebSocket: pandora-reconnect with account_id
  WebSocket->>Daemon: set active account and reconnect
```

---

## Testing and docs

- **Backend**: Unit or manual: single-account (user/password in main) and multi-account (account = id:path, no user/password in main); switch account via WebSocket with `account_id`; verify state and merge.
- **Web UI**: Manual: two accounts, switch via dropdown; confirm stations and playback reflect the selected account.
- **HA**: Manual: select entity shows accounts; changing it triggers reconnect and media player reflects new account.
- **Docs**: Update [WEBSOCKET_API.md](remote-pianobar/WEBSOCKET_API.md) (reconnect payload, state fields); add a short "Multiple accounts" section to the main README or config docs describing the include-file format (main = active, alternates override only).

---

## Summary

| Layer   | Change                                                                                                                                                                                                         |
| ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Config  | One account: credentials in main config. Multiple: `account = id:path` in main (no user/password in main); all credentials in separate files; merge on switch.                                                 |
| Backend | Account list + active index in settings; parse in BarSettingsRead; login/reconnect use active account; reconnect action accepts optional `account_id`; emit `current_account` and `accounts` in process state. |
| Web UI  | Account dropdown; on change send reconnect with `account_id`.                                                                                                                                                  |
| HA      | New Select entity for account; coordinator stores accounts/current_account; optional `account_id` on reconnect service.                                                                                        |

Implement backend first (config + reconnect + state), then Web UI, then HA, so the wire format is stable.
