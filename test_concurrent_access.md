# Concurrent Access Testing for Deadlock Fixes

This document describes how to test the thread safety improvements and deadlock fixes.

## Quick Test (Manual)

### 1. Start Pianobar in BOTH Mode

Ensure your `~/.config/pianobar/config` has:

```ini
ui_mode = both
```

Then start pianobar with debug logging:

```bash
PIANOBAR_DEBUG=8 ./pianobar
```

### 2. Open Multiple WebSocket Clients

Open 3-5 browser tabs to `http://localhost:8080` (or your configured WebUI URL).

### 3. Concurrent Play/Pause Test

**Goal:** Verify no redundant locking or deadlocks when multiple clients send play/pause commands.

1. From **Browser Tab 1**: Click Play button rapidly (5-10 times)
2. From **Browser Tab 2**: Click Pause button rapidly (5-10 times)
3. From **CLI**: Press 'p' (toggle pause) repeatedly
4. From **Browser Tab 3**: Click Play/Pause toggle rapidly

**Expected Result:**
- All commands are processed
- No hangs or deadlocks
- Debug log shows clean lock acquire/release pairs
- No "Lock acquired" without matching "Lock released"

**Look for in debug output:**
```
State: Lock acquired (GetCurrentStation)
State: GetCurrentStation -> Radio Paradise
State: Lock released (GetCurrentStation)
```

### 4. Concurrent Station Changes

1. From **Browser Tab 1**: Change station to Station A
2. Immediately from **Browser Tab 2**: Change station to Station B
3. From **CLI**: Press 's' and select Station C
4. From **Browser Tab 3**: Change station to Station D

**Expected Result:**
- Last command wins (final station is D)
- No crashes or inconsistent state
- All clients see the final state correctly

### 5. Concurrent Volume Changes

1. From **Browser Tab 1**: Move volume slider rapidly
2. From **Browser Tab 2**: Move volume slider in opposite direction
3. From **CLI**: Press '+' and '-' keys rapidly

**Expected Result:**
- Volume changes smoothly
- No stuttering or hangs
- Final volume reflects last command

### 6. Mixed Operations Test

Perform all operations simultaneously:
- **Tab 1**: Change stations repeatedly
- **Tab 2**: Click play/pause rapidly
- **Tab 3**: Adjust volume
- **Tab 4**: Love/ban songs
- **CLI**: Press random keys (s, p, +, -, n, etc.)

**Expected Result:**
- All operations complete
- No deadlocks or hangs
- State remains consistent across all clients

## Advanced Test (Automated)

### Using curl for Rapid Commands

```bash
# Terminal 1: Start pianobar
PIANOBAR_DEBUG=8 ./pianobar

# Terminal 2: Send 100 play commands
for i in {1..100}; do
    curl -X POST http://localhost:8080/api/action/play &
done

# Terminal 3: Send 100 pause commands simultaneously
for i in {1..100}; do
    curl -X POST http://localhost:8080/api/action/pause &
done

# Wait for all background jobs
wait
```

**Expected Result:**
- All 200 commands complete
- No timeouts or errors
- Final state is either playing or paused (last command wins)

### Stress Test with WebSocket Events

Create a simple Node.js script (`stress_test.js`):

```javascript
const io = require('socket.io-client');

const NUM_CLIENTS = 10;
const COMMANDS_PER_CLIENT = 50;

const clients = [];

for (let i = 0; i < NUM_CLIENTS; i++) {
    const socket = io('http://localhost:8080');
    
    socket.on('connect', () => {
        console.log(`Client ${i} connected`);
        
        // Send random commands
        let count = 0;
        const interval = setInterval(() => {
            const commands = ['playback.play', 'playback.pause', 'playback.next'];
            const cmd = commands[Math.floor(Math.random() * commands.length)];
            socket.emit('action', cmd);
            
            count++;
            if (count >= COMMANDS_PER_CLIENT) {
                clearInterval(interval);
                socket.disconnect();
            }
        }, 10); // Send command every 10ms
    });
    
    clients.push(socket);
}
```

Run:

```bash
npm install socket.io-client
node stress_test.js
```

**Expected Result:**
- All 500 commands (10 clients × 50 commands) are processed
- No deadlocks or crashes
- Debug log shows orderly lock acquisition/release

## What to Look For

### Good Signs ✓

1. **Clean lock pairs:**
   ```
   State: Lock acquired (GetCurrentStation)
   State: Lock released (GetCurrentStation)
   ```

2. **Short lock duration:**
   - Locks held for < 1ms (check timestamps)

3. **No nested locks:**
   - No "Lock acquired" before previous "Lock released"

4. **Consistent state:**
   - All clients show same station, play state, volume

### Bad Signs ✗

1. **Unpaired locks:**
   ```
   State: Lock acquired (GetCurrentStation)
   State: Lock acquired (GetPlaylist)  ← Missing unlock!
   ```

2. **Long lock duration:**
   - Locks held for > 10ms indicate blocking under lock

3. **Nested locks:**
   ```
   State: Lock acquired (SwitchStation)
   State: Lock acquired (SwitchStation)  ← Deadlock!
   ```

4. **Hangs:**
   - Process stops responding
   - WebUI becomes unresponsive
   - CLI input ignored

5. **Inconsistent state:**
   - Different clients show different stations
   - Play/pause state desynchronized

## Debug Build Testing

For deeper validation, build with debug assertions:

```bash
make WEBSOCKET=1 clean
make WEBSOCKET=1 DEBUG=1
PIANOBAR_DEBUG=8 ./pianobar
```

Debug assertions will catch:
- Lock ordering violations
- Functions called without required locks held
- Locks held when they shouldn't be

If an assertion fires, you'll see:
```
Assertion failed: (stateMutex is NOT held), function xyz, file bar_state.c, line 123
```

## Regression Testing Checklist

Before releasing deadlock fixes, verify:

- [ ] CLI-only mode still works (`ui_mode = cli`)
- [ ] WebUI-only mode works (`ui_mode = web`)
- [ ] BOTH mode works (`ui_mode = both`)
- [ ] Rapid play/pause from multiple clients
- [ ] Concurrent station changes
- [ ] Volume changes during playback
- [ ] Station switch during song playback
- [ ] Love/ban while changing stations
- [ ] Network interruption during API call (doesn't deadlock)
- [ ] Debug build with assertions passes all tests

## Known Safe Patterns

These patterns are verified to be deadlock-free:

1. **Play/Pause from WebSocket:**
   - `BarSocketIoHandleAction` → `BarUiDispatchById` → `BarUiActPlay`
   - Locks: `player.lock` acquired once, released before broadcast
   - ✓ No redundant lock acquisition (fixed)

2. **Station Change from WebSocket:**
   - `BarSocketIoHandleChangeStation` → `BarStateFindStationById` → `BarUiSwitchStation`
   - Locks: `stateMutex` acquired/released in each function
   - ✓ No nested locking

3. **Pandora API Call:**
   - `BarStateCallPandora` → `BarUiPianoCall`
   - Locks: `stateMutex` held during entire API call (by design)
   - ✓ No other locks acquired under `stateMutex`

4. **Progress Broadcast:**
   - `BarWebsocketBroadcastProgress` → `BarSocketIoEmitProgress`
   - Locks: `player.lock` only (not `stateMutex`)
   - ✓ Single lock, minimal duration

## Performance Benchmarks

Optional: Measure lock contention before/after fixes:

```bash
# Compile with -pg for profiling
make WEBSOCKET=1 clean
CFLAGS="-pg" make WEBSOCKET=1

# Run stress test
./stress_test.sh

# Analyze with gprof
gprof pianobar gmon.out > profile.txt
```

Look for:
- Reduced time in `pthread_mutex_lock` calls
- Fewer context switches
- Lower CPU usage under concurrent load

## Conclusion

If all tests pass:
- ✓ No deadlocks detected
- ✓ Thread safety verified
- ✓ Performance improved (redundant lock removed)
- ✓ Ready to commit and merge

See `src/THREAD_SAFETY.md` for detailed documentation on lock hierarchy and safe patterns.

