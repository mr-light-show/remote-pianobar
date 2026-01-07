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
| `audio_backend` | `auto` | Audio backend: `auto`, `pulseaudio`, `alsa`, `jack`, `coreaudio` (macOS), `wasapi` (Windows). Auto-detection recommended. |
| `volume` | `50` | Initial volume (0-100 linear scale) |
| `system_volume_player_gain` | `75` | Player gain for system volume mode (0-100). Lower values leave more headroom for ReplayGain boosts. |
| `gain_mul` | `1.0` | ReplayGain multiplier (scales the track's ReplayGain value) -- Reduce if clipping occurs on some songs|
| `sample_rate` | `0` (stream default) | Force specific sample rate (Hz) |
| `buffer_seconds` | `5` | Audio buffer size in seconds |
| `audio_pipe` | (none) | Path to audio output pipe |

### Audio Backend Selection

Pianobar uses miniaudio for audio playback, which supports multiple backends:

- **auto** (default): Automatically selects the best available backend for your system
- **pulseaudio**: Linux PulseAudio (most modern Linux desktops)
- **alsa**: Linux ALSA (direct hardware access)
- **jack**: JACK Audio Connection Kit (professional audio)
- **coreaudio**: macOS Core Audio
- **wasapi**: Windows WASAPI

The `auto` setting is recommended for most users. Miniaudio will automatically detect and use the best backend for your platform. Manual backend selection is only needed for troubleshooting or special audio routing requirements.

**Note:** Miniaudio provides automatic device switching when the default audio output changes (e.g., plugging/unplugging headphones). This works automatically in `auto` mode.

### Volume Control

The volume system uses a simple linear 0-100 scale:
- **Player mode** (`volume_mode = player`): Volume slider controls digital gain (0-100%)
- **System mode** (`volume_mode = system`): Volume slider controls OS mixer; player uses `initial_player_gain`

ReplayGain is applied on top of the volume setting. The default `system_volume_player_gain` of 75% leaves headroom for ReplayGain to boost quiet tracks without clipping.

## Interface/Display

| Option | Default | Description |
|--------|---------|-------------|
| `autoselect` | `true` | Auto-select station on startup |
| `autostart_station` | (none) | Station ID to auto-start. Set to empty (`autostart_station = `) to prevent auto-play and wait for manual selection. |
| `history` | `5` | Number of songs to keep in history |
| `pause_timeout` | `30` (minutes) | Auto-stop after this many minutes of pause. Set to `0` to disable. When triggered, clears playlist/station and disconnects from Pandora. |
| `sort` | `name_az` | Station sort order (see below) |
| `love_icon` | ` <3` | Icon for loved songs |
| `ban_icon` | ` </3` | Icon for banned songs |
| `tired_icon` | ` zZ` | Icon for tired songs |
| `at_icon` | ` @ ` | Separator icon |

### Notes on Station Auto-Start

**State File Behavior:**
- Pianobar automatically saves the last played station to `~/.config/pianobar/state`
- On next startup, this station will auto-play unless overridden in your config
- The config file (`~/.config/pianobar/config`) is read **after** the state file and takes precedence

**To prevent auto-play on startup:**

Set an empty value in your config file to override the state file:
```ini
autostart_station = 
```

This is particularly useful for:
- Web-only mode (`ui_mode = web`) - wait for station selection via web UI
- Headless installations - prevent unexpected audio playback on boot
- Shared systems - let each user choose their station

**To auto-play a specific station:**

Find the station ID by running pianobar in CLI mode, then use it in your config:
```ini
autostart_station = 122728589551497388
```

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

## Remote API/Web UI Options

These options are only available when pianobar is compiled with `WEBSOCKET_ENABLED`.

| Option | Default | Description |
|--------|---------|-------------|
| `ui_mode` | `both` | UI mode: `cli` (CLI only), `web` (Remote API - headless daemon), or `both` (CLI+Remote API) |
| `websocket_port` | (none) | WebSocket server port (e.g., `9001`) - **required** for web UI |
| `websocket_host` | `0.0.0.0` | WebSocket bind address (use `127.0.0.1` for localhost only) |
| `webui_path` | (none) | Custom path to web UI files (defaults to built-in) |
| `pid_file` | (none) | Path to PID file for daemon mode |
| `log_file` | (none) | Path to log file for daemon mode |

**Security Note:** By default, `websocket_host` is set to `0.0.0.0`, which binds the web interface to all network interfaces, making it accessible from remote machines. The web interface has no built-in authentication. For localhost-only access, set `websocket_host = 127.0.0.1` in your config. Consider using a firewall to restrict access to trusted IP addresses when exposing the interface remotely.

**Note on `ui_mode` values:**
- `cli`: Traditional command-line interface only, Remote API disabled
- `web`: Headless Remote API (web-only) mode - pianobar daemonizes and runs in background, returning control to shell
- `both`: Both CLI and Remote API enabled, runs in foreground (default)

When using `ui_mode = web`, you should also configure:
- `pid_file`: Location to write process ID (for managing the daemon)
- `log_file`: Location for log output (since stdout/stderr are redirected)

### Web UI Example Configuration

```ini
ui_mode = both
websocket_port = 9001
webui_enabled = 1
# websocket_host = 0.0.0.0  # Default - accessible from remote machines
# websocket_host = 127.0.0.1  # Localhost only - more secure
```

Access the web interface at `http://YOUR_IP:9001/` or `http://localhost:9001/`

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

# Prevent auto-play - wait for station selection (overrides state file)
autostart_station = 

# Volume (0-100 linear scale, 50 = default)
volume = 50

# WebSocket/Web UI (optional)
ui_mode = both
websocket_port = 9001

# Event script for notifications (optional)
event_command = /home/user/.config/pianobar/eventcmd
```

For a complete example with all available options, see `contrib/config-example`.

## See Also

- `README.rst` - General usage and installation
- `contrib/config-example` - Example configuration file
- `contrib/eventcmd-examples/` - Example event command scripts

