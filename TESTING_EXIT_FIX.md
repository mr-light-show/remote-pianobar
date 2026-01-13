# Testing the Exit Speed Fix

## What Was Changed

Modified `src/player.c` in the `BarPlayerDestroy()` function to fix slow exit times on Ubuntu/Linux.

### Before:
```c
void BarPlayerDestroy(player_t * const p) {
    if (p->engineInitialized) {
        ma_engine_uninit(&p->engine);  // ← This took 12 seconds on Ubuntu
        p->engineInitialized = false;
    }
    // ...
}
```

### After:
```c
void BarPlayerDestroy(player_t * const p) {
    if (p->engineInitialized) {
        ma_engine_stop(&p->engine);     // ← Force stop first (prevents drain delay)
        ma_engine_uninit(&p->engine);   // ← Now fast on Ubuntu
        p->engineInitialized = false;
    }
    // ...
}
```

## Root Cause

On Linux (ALSA/PulseAudio), `ma_engine_uninit()` waits for audio buffers to drain completely before destroying the engine. This takes ~12 seconds. On macOS (CoreAudio), this happens instantly.

By calling `ma_engine_stop()` first, we force the audio device to stop immediately rather than waiting for the drain.

## How to Test on Ubuntu

### Test 1: Exit After Playing Music

1. Start pianobar with debug output:
   ```bash
   PIANOBAR_DEBUG=8 ./pianobar
   ```

2. Let a song play for at least 10 seconds

3. Press `q` to quit

4. Observe the timing in the debug output:
   ```
   (i) Exiting...
   PlaybackMgr: Thread stopped
   BarPlayerDestroy: Stopping engine before uninit
   BarPlayerDestroy: Calling ma_engine_uninit
   BarPlayerDestroy: ma_engine_uninit completed    ← Should be < 1 second
   WebSocket: Stopping server...
   ```

5. **Expected result:** Exit completes in < 2 seconds (previously 12-15 seconds)

### Test 2: Exit After Pausing

1. Start pianobar, let music play
2. Pause with `p`
3. Press `q` to quit
4. **Expected result:** Still fast exit (< 2 seconds)

### Test 3: Exit Without Playing

1. Start pianobar
2. Press `q` immediately (before any music plays)
3. **Expected result:** Instant exit (this should already be fast)

## Debug Output to Look For

With `PIANOBAR_DEBUG=8`, you should see:

```
BarPlayerDestroy: Stopping engine before uninit
BarPlayerDestroy: Calling ma_engine_uninit
BarPlayerDestroy: ma_engine_uninit completed
```

The time between "Calling" and "completed" should be < 1 second (down from 12 seconds).

## If Still Slow

If the exit is still slow (> 3 seconds), we'll need to implement Option 2 (timeout wrapper). Please report:

1. Exact timing from debug output
2. Your audio backend (PulseAudio/ALSA/other)
3. Ubuntu version
4. Run: `pactl info | grep "Server Name"` to check audio server

## Deployment

After testing confirms the fix works:

1. The compiled binary is at: `remote-pianobar/pianobar`
2. You can copy it to Ubuntu: `scp pianobar user@ubuntu:/usr/local/bin/`
3. Or rebuild on Ubuntu directly with the updated source

## Reverting if Needed

If this causes any issues, the old behavior can be restored by removing the `ma_engine_stop()` call:

```c
void BarPlayerDestroy(player_t * const p) {
    if (p->engineInitialized) {
        // Remove this line to revert:
        // ma_engine_stop(&p->engine);
        
        ma_engine_uninit(&p->engine);
        p->engineInitialized = false;
    }
    // ...
}
```
