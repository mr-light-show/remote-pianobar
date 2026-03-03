---
name: Step 5 – Code duplication
overview: Unify BarTransformIfShared/BarWsTransformIfShared in ui_act.c and extract ALSA open/find helper in system_volume.c.
todos:
  - id: dup-transform
    content: Unify BarTransformIfShared and BarWsTransformIfShared – single function with report-progress parameter or callback
  - id: dup-alsa
    content: Extract alsa_open_mixer_and_find_elem() in system_volume.c; use from alsaGetVolume and alsaSetVolume
isProject: false
---

# Step 5: Code duplication

**Identified cases:**

1. **Transform station:** [src/ui_act.c](src/ui_act.c) lines 93–108 (`BarTransformIfShared`) vs 113–129 (`BarWsTransformIfShared`). Logic is the same; only the "log/feedback" differs (BarUiMsg + BarUiPianoCall vs debugPrint + BarUiPianoCallLogged). **Recommendation:** Single function with a parameter or callback for "report progress" (e.g. enum or function pointer), or a small helper that takes a "logger" and call it from both call sites.
2. **ALSA volume:** [src/system_volume.c](src/system_volume.c) — `alsaGetVolume` and `alsaSetVolume` duplicate the same open/attach/register/load/find_selem sequence and both use `goto error` + `snd_mixer_close(handle)`. **Recommendation:** Extract a helper e.g. `alsa_open_mixer_and_find_elem(snd_mixer_t **handle, snd_mixer_elem_t **elem)` that performs the common setup and returns success/failure; call it from both get and set, then do get/set-specific work.
