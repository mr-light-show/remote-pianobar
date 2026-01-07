# Ring Buffer Investigation Results

**Date:** January 6, 2026

## Question
Can we remove the ring buffer to simplify the code?

## Test Conducted
Created a separate test build (`remote-pianobar-no-ringbuffer`) that removed the ring buffer and had the miniaudio callback pull audio directly from the ffmpeg filter.

## Test Results

### WITHOUT Ring Buffer:
❌ **Crackling occurred** - The original issue returned  
❌ **Playback stopped after 6 seconds** - Critical failure  
✅ **Pause/quit worked** - For the brief time it played

### Root Cause Analysis
The ring buffer is **essential** because:

1. **Timing mismatch**: The audio callback runs on a real-time thread with strict timing requirements (every 10-20ms). It cannot wait or block.

2. **Variable decode speed**: FFmpeg decoding is variable-speed - sometimes frames are ready immediately, sometimes they take longer.

3. **Decoupling needed**: The ring buffer decouples the real-time audio output from the variable-speed decoder. Without it:
   - When callback can't get frames fast enough → crackling/underruns
   - When callback tries to wait for frames → audio thread blocks → playback stops

4. **Buffer as shock absorber**: The ring buffer provides a ~3 second cushion that absorbs timing variations between decoder and playback.

## Conclusion

**The ring buffer MUST stay.** However, we identified and fixed its issues:

### Issues Fixed in This Session:

1. ✅ **Songs cutting off at end**
   - Added buffer drain logic to wait for ring buffer to empty before stopping
   
2. ✅ **Progress stops during drain**
   - Set `songPlayed = songDuration` immediately when drain starts
   
3. ✅ **Exiting while paused causes brief playback**
   - Check pause/quit state before attempting drain
   - Skip drain if paused or interrupted
   
4. ✅ **Reduced timeout**
   - Reduced drain timeout from 10s to 5s
   - Added `shouldQuit()` check in drain loop for immediate exit

### Changes Made to `finish()` function:

```c
static void finish (player_t * const player) {
    /* Wait for ring buffer to drain if decoding finished normally */
    if (player->decodingFinished && player->maDevice.pContext != NULL) {
        /* Check if we should skip drain (paused or quitting) */
        pthread_mutex_lock(&player->lock);
        bool isPaused = player->doPause;
        pthread_mutex_unlock(&player->lock);
        
        if (isPaused || shouldQuit(player)) {
            debugPrint(DEBUG_AUDIO, "Skipping buffer drain (paused=%d, quit=%d)\n", 
                      isPaused, shouldQuit(player));
        } else {
            /* Set progress to song duration immediately */
            pthread_mutex_lock(&player->lock);
            player->songPlayed = player->songDuration;
            pthread_mutex_unlock(&player->lock);
            
            int maxWaitMs = 5000;  /* 5 second timeout (reduced from 10s) */
            
            while (waitedMs < maxWaitMs) {
                /* Check if interrupted */
                if (shouldQuit(player)) {
                    debugPrint(DEBUG_AUDIO, "Buffer drain interrupted\n");
                    break;
                }
                
                /* ... drain logic ... */
            }
        }
    }
    /* ... rest of cleanup ... */
}
```

## Architecture (Final)

```
┌─────────────────┐
│ BarPlayerThread │
│  (decode loop)  │
└────────┬────────┘
         │
         ▼
┌─────────────────────┐
│   FFmpeg Filter     │
│   (av_buffersink)   │
└──────────┬──────────┘
           │
           ▼
┌────────────────────────┐
│ BarAudioProducerThread │
│  (pulls from filter,   │
│   writes to buffer)    │
└──────────┬─────────────┘
           │
           ▼
┌──────────────────┐
│   Ring Buffer    │
│  (3 sec audio)   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ maDataCallback   │
│ (reads buffer,   │
│  feeds device)   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│  Audio Device    │
└──────────────────┘
```

## Lessons Learned

1. **Real-time audio requires buffering** - Direct pull from decoder doesn't work
2. **Test intermediate builds** - We should have tested without ring buffer earlier
3. **Ring buffer complexity is justified** - The extra code solves real problems
4. **Buffer drain needs care** - Must handle pause, quit, and progress edge cases

## Testing Status

- ✅ Ring buffer necessity confirmed through testing
- ✅ Buffer drain fixes implemented
- ⏳ Need to test: songs play to completion with correct progress
- ⏳ Need to test: pause/quit works smoothly
- ⏳ Need to test: no brief playback when quitting while paused

