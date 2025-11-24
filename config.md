# Pianobar Configuration Guide

This document describes all available configuration options for pianobar.

## Configuration File Location

Pianobar looks for config files in:
1. `~/.config/pianobar/config` (primary configuration)
2. `~/.config/pianobar/state` (runtime state)

See `contrib/config-example` for a template configuration file.

## Authentication

| Option | Default | Description |
|--------|---------|-------------|
| `user` | (none) | Your Pandora email/username |
| `password` | (none) | Your Pandora password |
| `password_command` | (none) | Command to retrieve password (e.g., `gpg --decrypt ~/password`) |

## Network/Proxy

| Option | Default | Description |
|--------|---------|-------------|
| `control_proxy` | (none) | HTTP proxy for control connection |
| `proxy` | `$http_proxy` env var | HTTP proxy for audio streams |
| `bind_to` | (none) | Network interface to bind to (e.g., `if!tun0`) |
| `rpc_host` | `internal-tuner.pandora.com` | Pandora RPC host |
| `rpc_tls_port` | `443` | Pandora RPC TLS port |
| `ca_bundle` | system default | Path to CA certificates bundle |
| `timeout` | `30` (seconds) | Network timeout |
| `max_retry` | `5` | Max retries for network requests |

## Audio

| Option | Default | Description |
|--------|---------|-------------|
| `audio_quality` | `high` | Audio quality: `low`, `medium`, or `high` |
| `volume` | `0` (dB) | Initial volume (-40 to +20 dB) |
| `gain_mul` | `1.0` | Volume gain multiplier |
| `max_gain` | `10` (dB) | Maximum gain for volume slider at 100%. Recommended values: 10 (safe), 15 (more headroom), 20 (maximum) |
| `sample_rate` | `0` (stream default) | Force specific sample rate (Hz) |
| `buffer_seconds` | `5` | Audio buffer size in seconds |
| `audio_pipe` | (none) | Path to audio output pipe |

### Volume Control

The volume system uses a perceptual curve for smoother control:
- Range: -40 dB (minimum) to +`max_gain` dB (maximum)
- 0 dB at 50% slider position
- Lower half (-40 to 0 dB) uses squared curve for finer control
- Upper half (0 to +max_gain dB) uses linear curve

## Interface/Display

| Option | Default | Description |
|--------|---------|-------------|
| `autoselect` | `true` | Auto-select station on startup |
| `autostart_station` | (none) | Station ID to auto-start |
| `history` | `5` | Number of songs to keep in history |
| `sort` | `name_az` | Station sort order (see below) |
| `love_icon` | ` <3` | Icon for loved songs |
| `ban_icon` | ` </3` | Icon for banned songs |
| `tired_icon` | ` zZ` | Icon for tired songs |
| `at_icon` | ` @ ` | Separator icon |

### Sort Options

Available values for the `sort` setting:
- `name_az` - Alphabetical A-Z
- `name_za` - Alphabetical Z-A
- `quickmix_01_name_az` - Regular stations first, QuickMix last, then alphabetical A-Z
- `quickmix_01_name_za` - Regular stations first, QuickMix last, then alphabetical Z-A
- `quickmix_10_name_az` - QuickMix first, regular stations last, then alphabetical A-Z
- `quickmix_10_name_za` - QuickMix first, regular stations last, then alphabetical Z-A

## Format Strings

| Option | Default | Description |
|--------|---------|-------------|
| `format_nowplaying_song` | `"%t" by "%a" on "%l"%r%@%s` | Now playing song format |
| `format_nowplaying_station` | `Station "%n" (%i)` | Now playing station format |
| `format_list_song` | `%i) %a - %t%r` | Song list format |
| `format_time` | `%s%r/%t` | Time display format |

### Format Placeholders

- `%t` = Song title
- `%a` = Artist name
- `%l` = Album name
- `%r` = Rating icon (love/ban/tired)
- `%s` = Station name
- `%n` = Station name (alternate)
- `%i` = Station/song index
- `%d` = Duration
- `%e` = Elapsed time
- `%@` = At icon separator

## Message Formats

These control the prefix for different message types in the CLI.

| Option | Default | Description |
|--------|---------|-------------|
| `format_msg_none` | (none) | Prefix for MSG_NONE |
| `format_msg_info` | `(i) ` | Prefix for info messages |
| `format_msg_playing` | `\|>  ` | Prefix for playing messages |
| `format_msg_time` | `#   ` | Prefix for time messages |
| `format_msg_err` | `/!\\ ` | Prefix for error messages |
| `format_msg_question` | `[?] ` | Prefix for question prompts |
| `format_msg_list` | `\t` (tab) | Prefix for list items |

## Control

| Option | Default | Description |
|--------|---------|-------------|
| `fifo` | `~/.config/pianobar/ctl` | Path to control FIFO |
| `event_command` | (none) | Path to event command script |

The FIFO allows external programs to control pianobar. See `contrib/remote.sh` for examples.

## WebSocket/Web UI Options

These options are only available when pianobar is compiled with `WEBSOCKET_ENABLED`.

| Option | Default | Description |
|--------|---------|-------------|
| `ui_mode` | `both` | UI mode: `cli` (CLI only), `web` (headless), or `both` |
| `websocket_port` | (none) | WebSocket server port (e.g., `9001`) - **required** for web UI |
| `websocket_host` | `127.0.0.1` | WebSocket bind address (use `0.0.0.0` for all interfaces) |
| `webui_enabled` | `false` | Enable built-in web UI server |
| `webui_path` | (none) | Custom path to web UI files (defaults to built-in) |
| `pid_file` | (none) | Path to PID file for daemon mode |
| `log_file` | (none) | Path to log file for daemon mode |

### Web UI Example Configuration

```ini
ui_mode = both
websocket_port = 9001
websocket_host = 127.0.0.1
webui_enabled = 1
```

Access the web interface at `http://127.0.0.1:9001/`

## Keybindings

All keyboard shortcuts are configurable. To disable a keybinding, set it to an empty value (e.g., `act_debug = `).

### Playback Control

| Config Key | Default | Action |
|------------|---------|--------|
| `act_songplay` | `P` | Resume playback |
| `act_songpause` | `S` | Pause playback |
| `act_songpausetoggle` | `p` | Toggle pause/resume |
| `act_songpausetoggle2` | `Space` | Toggle pause/resume (alternate) |
| `act_songnext` | `n` | Skip to next song |

### Song Actions

| Config Key | Default | Action |
|------------|---------|--------|
| `act_songlove` | `+` | Love current song |
| `act_songban` | `-` | Ban current song |
| `act_songtired` | `t` | Mark song as tired (ban for 1 month) |
| `act_songexplain` | `e` | Explain why this song is playing |
| `act_songinfo` | `i` | Print song/station info |
| `act_bookmark` | `b` | Bookmark song/artist |

### Station Management

| Config Key | Default | Action |
|------------|---------|--------|
| `act_stationchange` | `s` | Change station |
| `act_stationcreate` | `c` | Create new station |
| `act_stationcreatefromsong` | `v` | Create station from current song/artist |
| `act_stationdelete` | `d` | Delete station |
| `act_stationrename` | `r` | Rename station |
| `act_stationaddmusic` | `a` | Add music to station |
| `act_stationaddbygenre` | `g` | Add genre station |
| `act_addshared` | `j` | Add shared station |
| `act_stationselectquickmix` | `x` | Select QuickMix stations |
| `act_managestation` | `=` | Manage station (seeds/feedback/mode) |

### Volume Control

| Config Key | Default | Action |
|------------|---------|--------|
| `act_volup` | `)` | Increase volume |
| `act_voldown` | `(` | Decrease volume |
| `act_volreset` | `^` | Reset volume to 0 dB |

### General

| Config Key | Default | Action |
|------------|---------|--------|
| `act_help` | `?` | Show help |
| `act_quit` | `q` | Quit pianobar |
| `act_history` | `h` | Show song history |
| `act_upcoming` | `u` | Show upcoming songs |
| `act_settings` | `!` | Change settings |
| `act_debug` | `$` | Debug info (hidden) |

## Pandora API Settings (Advanced)

These settings control the Pandora API connection. The defaults work correctly and rarely need changing.

| Option | Default | Description |
|--------|---------|-------------|
| `partner_user` | `android` | Pandora partner user |
| `partner_password` | `AC7IBG09A3DTSYM4R41UJWL07VLN8JI7` | Partner password |
| `device` | `android-generic` | Device identifier |
| `encrypt_password` | `6#26FRL$ZWD` | Encryption key (outkey) |
| `decrypt_password` | `R=U!LH$O2B#` | Decryption key (inkey) |

**Warning:** Changing these values may cause authentication to fail.

## Example Configuration

Here's a minimal working configuration:

```ini
# Authentication
user = your@email.com
password = your_password

# Audio quality
audio_quality = high

# Volume (0 dB is unchanged, negative is quieter, positive is louder)
volume = 0
max_gain = 10

# WebSocket/Web UI (optional)
ui_mode = both
websocket_port = 9001
webui_enabled = 1

# Event script for notifications (optional)
event_command = /home/user/.config/pianobar/eventcmd
```

For a complete example with all available options, see `contrib/config-example`.

## See Also

- `README.rst` - General usage and installation
- `contrib/config-example` - Example configuration file
- `contrib/eventcmd-examples/` - Example event command scripts
- `WEBSOCKET_WEB_INTERFACE_PLAN.md` - Web UI feature documentation

