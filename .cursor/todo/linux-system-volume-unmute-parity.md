# Linux system volume: unmute when raising level (parity with macOS)

**Context:** macOS fix in `src/system_volume.c` clears output mute when `BarSystemVolumeSet(percent)` is called with `percent > 0` (CoreAudio `kAudioDevicePropertyMute` + osascript `without output muted`). Linux backends still only adjust volume, not sink mute.

**Goal:** Same user-visible behavior — if the default sink is muted (e.g. user muted in `pavucontrol` or DE), raising volume from Remote Pianobar should unmute and apply the new level.

## Tasks

- [ ] **PulseAudio (`HAVE_PULSEAUDIO`):** In `pulseaudioSetVolume`, when `percent > 0`, call `pa_context_set_sink_mute_by_name(paContext, "@DEFAULT_SINK@", 0, ...)` before (or after) `pa_context_set_sink_volume_by_name`, using the same mainloop iteration + `paMutex` pattern as the existing set-volume path.
- [ ] **pactl fallback:** In `pactlSetVolume`, when `percent > 0`, run `pactl set-sink-mute @DEFAULT_SINK@ 0` (e.g. via `system()` or `snprintf` + `system`) before the existing `set-sink-volume` command; treat failures consistently with current volume command.
- [ ] **ALSA (optional, lower priority):** In `alsaSetVolume`, if `snd_mixer_selem_has_playback_switch` and `percent > 0`, call `snd_mixer_selem_set_playback_switch_all(elem, 0)` to unmute (ALSA: 0 = off = not muted for typical Master playback switches; confirm on target hardware). Test on at least one desktop and one Pi-class config if included.

## Verification

- Mute default sink (`pactl set-sink-mute @DEFAULT_SINK@ 1`), set `volume_mode = system`, raise volume from web UI — audio and sink state should recover.
- `make test-all` on Linux after changes.

## References

- Plan: `.cursor/plans/macos_system_volume_mute_035e4ed8.plan.md` (Linux section).
- File: `src/system_volume.c` (`pulseaudioSetVolume`, `pactlSetVolume`, `alsaSetVolume`).
