# Missing WebSocket Commands: Detailed Analysis

Two CLI commands are implemented but not yet available in the WebSocket interface: **h** (history) and **j** (add shared station). Here's a detailed analysis of each.

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

## Command: `j` - Add Shared Station

### CLI Implementation
- **File**: [`src/ui_act.c:210-226`](src/ui_act.c)
- **Function**: `BarUiActAddSharedStation()`
- **Command ID**: `BAR_KS_ADDSHARED`

### User Flow

1. **Prompt for station ID**
   - "Station id: "
   - Accepts only digits (0-9)
   - User enters numeric station ID (e.g., "123456789")

2. **Create station request**
   - Sets `reqData.token` to the entered station ID
   - Sets `reqData.type = PIANO_MUSICTYPE_INVALID`
   - Calls `PIANO_REQUEST_CREATE_STATION`

3. **Pandora processes**
   - Validates station ID
   - Adds station to user's account
   - Returns station details

4. **Event command**
   - Triggers `"stationaddshared"` event for external scripts

### Pandora API Interaction

**API Call**: `station.addStation` (via `PIANO_REQUEST_CREATE_STATION`)

**Request** ([`src/libpiano/request.c:238-250`](src/libpiano/request.c)):
```json
{
  "trackToken": "123456789",  // The shared station ID
  "musicType": null           // Not specified for shared stations
}
```

**Response**: Standard station object added to user's station list

**Key Point**: Shared stations are created the same way as other stations, but using a station ID as the token instead of a musicId.

### WebSocket Implementation Approach

**Simple single-step command:**
```json
// Client sends:
{
  "event": "station.addShared",
  "stationId": "123456789"
}

// Server broadcasts on success:
{
  "event": "stations",
  "stations": [ /* updated station list */ ]
}
```

### Complexity for WebSocket
**Low** - Very straightforward:
- Single new event handler: `station.addShared`
- Reuses existing create station logic
- Just needs station ID validation (digits only)
- Broadcasts updated station list on success

---

## Summary & Recommendations

| Command | Complexity | Pandora API | Status |
|---------|-----------|-------------|--------|
| **j** (shared station) | Low | `station.addStation` | ✅ **IMPLEMENTED** |
| **h** (history) | Medium | None (local) | ⏳ Recommended for implementation |
| ~~**b** (bookmark)~~ | N/A | Discontinued (Aug 2022) | ❌ Skip |
| ~~**!** (settings)~~ | N/A | Doesn't persist to config | ❌ Skip |

### Implementation Status

#### ✅ **j** - Add Shared Station (COMPLETED)
**Implementation Details:**
- **Backend:** Added `BarSocketIoHandleAddShared()` handler in `socketio.c`
- **Frontend:** Integrated into Create Station modal with dedicated input field
- **UI Location:** Create Station dialog → "Enter shared station ID (digits only)" input
- **Validation:** Client-side (digits only) and server-side validation
- **Event:** `station.addShared` with `{ stationId: "123456789" }`
- **User Flow:** User enters numeric station ID → clicks "Add" → station added to account
- **Integration:** Consolidated with other station creation methods (search, genre, current song/artist)

**Commits:**
- Initial implementation: Added shared station input and backend handler
- UI consolidation: Merged genre station into unified Create Station modal

#### ⏳ **h** - History (Recommended)
**Priority:** Medium
- More complex but valuable UX feature
- Enables user to review and act on past songs
- No Pandora API needed (local data)
- Would require new WebSocket events for querying and acting on historical songs

