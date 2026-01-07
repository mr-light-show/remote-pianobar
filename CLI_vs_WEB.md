# Missing WebSocket Commands: Detailed Analysis

CLI commands not yet available in the WebSocket interface.

**Commands excluded from implementation:**
- **b** (bookmark) - Pandora discontinued this feature in August 2022
- **!** (settings) - Changes don't persist to config file, only temporary session changes

---

## Command: `h` - Song History

### CLI Implementation
- **File**: [`src/ui_act.c:606-641`](src/ui_act.c)
- **Function**: `BarUiActHistory()`
- **Command ID**: `BAR_KS_HISTORY`

### User Flow

1. **Check history availability**
   - If `app->songHistory == NULL`: Show error message
   - If history disabled (`app->settings.history == 0`): "History disabled"
   - Otherwise: Present song list from history

2. **User selects song** via `BarUiSelectSong()`
   - Displays numbered list of previously played songs
   - User enters number to select

3. **Find original station**
   - Look up station by `histSong->stationId`
   - If station deleted: Show error and exit

4. **Interactive action loop**
   - Prompt: "What to do with this song?"
   - Available actions for history songs (require only `BAR_DC_SONG`):
     - **+** - Love song
     - **-** - Ban song
     - **t** - Tired of song (ban for 1 month)
     - **e** - Explain why this song played
     - **v** - Create station from song/artist
   - Actions operate on historical song, not current playing song
   - Loop continues until user cancels or action completes
   - **Note**: 'i' (info) is NOT available because it requires `BAR_DC_GLOBAL` context

### Pandora API Interaction
**None** - This is entirely local. History is stored in memory (`app->songHistory` linked list).

### WebSocket Implementation Approach

**Step 1: Query History**
```json
// Client sends:
{ "event": "query.history" }

// Server responds:
{
  "event": "historyList",
  "songs": [
    {
      "title": "Song Title",
      "artist": "Artist Name",
      "album": "Album Name",
      "stationId": "123456",
      "stationName": "Station Name",
      "coverArt": "https://...",
      "musicId": "...",
      "trackToken": "..."
    },
    // ... more songs
  ]
}
```

**Step 2: Act on Historical Song**
- User selects song from list (client-side)
- Client sends normal song command with historical song data
- Example: Love a song from history
```json
{
  "event": "song.love",
  "musicId": "...",
  "trackToken": "...",
  "stationId": "123456"
}
```

### Complexity for WebSocket
**Medium** - Requires:
- New event to fetch history list
- Serializing song history with full metadata
- Server must handle song commands with explicit song data (not just current song)
- Client UI to display and select from history

---

## Summary

| Command | Complexity | Pandora API | Status |
|---------|-----------|-------------|--------|
| **h** (history) | Medium | None (local) | ⏳ Recommended |
| ~~**b** (bookmark)~~ | N/A | Discontinued (Aug 2022) | ❌ Skip |
| ~~**!** (settings)~~ | N/A | Doesn't persist to config | ❌ Skip |
