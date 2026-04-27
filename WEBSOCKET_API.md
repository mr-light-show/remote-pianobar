# Pianobar WebSocket API Reference

This document provides a complete reference for the pianobar-websockets Socket.IO-based WebSocket API. It is designed to enable developers to build a fully functional Pandora web client without needing to reference source code.

## Table of Contents

1. [Connection](#connection)
2. [Message Format](#message-format)
3. [Server-to-Client Events](#server-to-client-events)
4. [Client-to-Server Events](#client-to-server-events)
5. [Action Commands](#action-commands)
6. [Data Structures](#data-structures)
7. [Volume System](#volume-system)
8. [Common Workflows](#common-workflows)
9. [Not Implemented](#not-implemented)

---

## Connection

### Endpoint

```
ws://host:port/socket.io
wss://host:port/socket.io  (for HTTPS)
```

### Subprotocol

The WebSocket connection **must** specify the `socketio` subprotocol.

### Example - Establishing Connection

```javascript
const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
const ws = new WebSocket(`${protocol}//${window.location.host}/socket.io`, 'socketio');

ws.onopen = () => {
  console.log('Connected to pianobar');
  // Request initial state
  ws.send('2["query",null]');
};

ws.onclose = () => {
  console.log('Disconnected from pianobar');
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};
```

### Connection Lifecycle

1. Client opens WebSocket connection with `socketio` subprotocol
2. On successful connection, client should send `query` event to request current state
3. Server responds with `process` event (full state) and `stations` event (station list)
4. Server broadcasts events as state changes occur
5. Client can send commands at any time while connected

---

## Message Format

This API uses a simplified Socket.IO protocol format.

### Packet Structure

All messages use packet type `2` (EVENT) with the format:

```
2["eventName", data]
```

- `2` - Socket.IO packet type for EVENT
- `eventName` - String identifying the event
- `data` - JSON payload (can be null, string, number, object, or array)

### Sending Messages

```javascript
// Simple action (string payload)
ws.send('2["action","playback.next"]');

// Event with object payload
ws.send('2["station.change",{"id":"123456789"}]');

// Event with null payload
ws.send('2["query",null]');

// Event with array payload
ws.send('2["station.setQuickMix",["456","789","012"]]');
```

### Receiving Messages

```javascript
ws.onmessage = (event) => {
  const message = event.data;
  
  // Socket.IO EVENT messages start with "2"
  if (typeof message === 'string' && message.startsWith('2')) {
    const jsonStr = message.substring(1);  // Remove packet type prefix
    const [eventName, eventData] = JSON.parse(jsonStr);
    
    console.log('Received event:', eventName, eventData);
    
    // Handle specific events
    switch (eventName) {
      case 'process':
        handleProcessEvent(eventData);
        break;
      case 'start':
        handleSongStart(eventData);
        break;
      case 'progress':
        handleProgress(eventData);
        break;
      // ... handle other events
    }
  }
};
```

---

## Server-to-Client Events

These events are broadcast from the server to connected clients.

### `process` - Full Application State

Sent in response to a `query` event or on initial connection. Contains complete application state.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `playing` | boolean | Whether a song is currently loaded |
| `paused` | boolean | Whether playback is paused |
| `volume` | number | Current volume as percentage (0-100) |
| `maxGain` | number | Maximum volume gain from config (internal use) |
| `station` | string | Current station name (empty string if none) |
| `stationId` | string | Current station ID (empty string if none) |
| `elapsed` | number | Current playback position in seconds (only if playing) |
| `song` | object | Current song object (only if playing) |
| `current_account` | object | Active account (only when multiple accounts configured). See [Account Object](#account-object). |
| `accounts` | array | List of all configured accounts (only when multiple accounts configured). See [Account Object](#account-object). |

**Example:**

```json
{
  "playing": true,
  "paused": false,
  "volume": 65,
  "maxGain": 10,
  "station": "Today's Hits Radio",
  "stationId": "3914377188324099182",
  "elapsed": 45,
  "song": {
    "title": "Blinding Lights",
    "artist": "The Weeknd",
    "album": "After Hours",
    "coverArt": "https://content-images.p-cdn.com/images/...",
    "rating": 1,
    "duration": 200,
    "trackToken": "S1234567...",
    "songStationName": "Today's Hits Radio"
  },
  "current_account": { "id": "work", "label": "Work" },
  "accounts": [
    { "id": "default", "label": "Home" },
    { "id": "work", "label": "Work" }
  ]
}
```

**Example (no song playing, single account):**

```json
{
  "playing": false,
  "paused": false,
  "volume": 50,
  "maxGain": 10,
  "station": "",
  "stationId": ""
}
```

> **Note:** `current_account` and `accounts` are only present when the server is configured with multiple Pandora accounts. Single-account setups omit these fields.

> **Note (rating):** After a successful `song.love` or `song.ban`, the server broadcasts a **`process`** event (not `start`) with the current track’s updated `song.rating`, `elapsed`, and other state—same `process` shape as after `query` or on connect. This avoids treating an in-place rating change like a new track. Clients that only handle `start` for now-playing should also handle `process` for that refresh. If `song.ban` skips to another track, a normal **`start`** for the *next* song may follow.

---

### `start` - Song Started Playing

Broadcast when a **new** song begins playing (e.g. station change, skip, or auto-advance). In-place metadata updates (such as `song.rating` after love/ban on the *same* track) are delivered via **`process`**, not `start` (see the `process` section above).

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `title` | string | Song title |
| `artist` | string | Artist name |
| `album` | string | Album name |
| `coverArt` | string | URL to album art image |
| `rating` | number | 0 = no rating, 1 = loved |
| `duration` | number | Song length in seconds |
| `trackToken` | string | Unique token for this track (used for station creation) |
| `songStationName` | string | Name of station this song came from |
| `station` | string | Current station name |
| `stationId` | string | Current station ID |

**Example:**

```json
{
  "title": "Bohemian Rhapsody",
  "artist": "Queen",
  "album": "A Night at the Opera",
  "coverArt": "https://content-images.p-cdn.com/images/abc123.jpg",
  "rating": 0,
  "duration": 354,
  "trackToken": "S9876543210abcdef",
  "songStationName": "Classic Rock Radio",
  "station": "Classic Rock Radio",
  "stationId": "1234567890123456789"
}
```

---

### `stop` - Song Stopped

Broadcast when playback stops (song ended, skipped, or station changed).

**Payload:** `null`

**Example:**

```json
null
```

---

### `progress` - Playback Progress

Broadcast periodically during playback (typically every second).

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `elapsed` | number | Seconds elapsed |
| `duration` | number | Total song duration in seconds |
| `percentage` | number | Progress percentage (0-100) |

**Example:**

```json
{
  "elapsed": 67,
  "duration": 240,
  "percentage": 27.916666666666668
}
```

---

### `volume` - Volume Changed

Broadcast when volume level changes.

**Payload:** `number` - Volume as percentage (0-100)

**Example:**

```json
75
```

---

### `playState` - Play/Pause State Changed

Broadcast when playback is paused or resumed.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `paused` | boolean | Current pause state |

**Example:**

```json
{
  "paused": true
}
```

---

### `stations` - Station List

Broadcast when the station list is updated: after `query` / `query.stations` (per client), on reconnect, after the initial `getStationList` completes, or when stations change (created/deleted/renamed).

**Payload:** Array of station objects

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Station ID |
| `name` | string | Station name |
| `isQuickMix` | boolean | True if this is the QuickMix station |
| `isQuickMixed` | boolean | True if this station is included in QuickMix |

**Example:**

```json
[
  {
    "id": "3914377188324099182",
    "name": "QuickMix",
    "isQuickMix": true,
    "isQuickMixed": false
  },
  {
    "id": "4506787012345678901",
    "name": "Today's Hits Radio",
    "isQuickMix": false,
    "isQuickMixed": true
  },
  {
    "id": "5612345678901234567",
    "name": "Classic Rock Radio",
    "isQuickMix": false,
    "isQuickMixed": true
  },
  {
    "id": "6789012345678901234",
    "name": "Jazz Classics",
    "isQuickMix": false,
    "isQuickMixed": false
  }
]
```

---

### `song.explanation` - Song Recommendation Explanation

Sent in response to a `song.explain` action. Contains Pandora's explanation for why the current song was chosen.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `explanation` | string | Text explanation from Pandora |

**Example:**

```json
{
  "explanation": "Based on what you've told us so far, we're playing this track because it features classic rock roots, a pointed lyrical style, repetitive melodic phrasing, and many other similarities identified in the Music Genome Project."
}
```

---

### `query.upcoming.result` - Upcoming Songs

Sent in response to a `query.upcoming` action. Contains list of upcoming songs in the queue.

**Payload:** Array of song objects

| Field | Type | Description |
|-------|------|-------------|
| `title` | string | Song title |
| `artist` | string | Artist name |
| `album` | string | Album name |
| `coverArt` | string | URL to album art |
| `rating` | number | 0 = no rating, 1 = loved |
| `duration` | number | Song length in seconds |
| `stationName` | string | Station this song came from |

**Example:**

```json
[
  {
    "title": "Hotel California",
    "artist": "Eagles",
    "album": "Hotel California",
    "coverArt": "https://content-images.p-cdn.com/images/...",
    "rating": 0,
    "duration": 391,
    "stationName": "Classic Rock Radio"
  },
  {
    "title": "Stairway to Heaven",
    "artist": "Led Zeppelin",
    "album": "Led Zeppelin IV",
    "coverArt": "https://content-images.p-cdn.com/images/...",
    "rating": 1,
    "duration": 482,
    "stationName": "Classic Rock Radio"
  }
]
```

---

### `genres` - Genre Categories

Sent in response to a `station.getGenres` event. Contains hierarchical list of genre categories and their stations.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `categories` | array | Array of genre category objects |

**Category Object:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Category name (e.g., "Rock", "Pop", "Jazz") |
| `genres` | array | Array of genre objects in this category |

**Genre Object:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Genre name |
| `musicId` | string | Music ID used to create a station |

**Example:**

```json
{
  "categories": [
    {
      "name": "Rock",
      "genres": [
        { "name": "Classic Rock", "musicId": "G100" },
        { "name": "Alternative Rock", "musicId": "G101" },
        { "name": "Indie Rock", "musicId": "G102" },
        { "name": "Hard Rock", "musicId": "G103" }
      ]
    },
    {
      "name": "Pop",
      "genres": [
        { "name": "Today's Hits", "musicId": "G200" },
        { "name": "80s Pop", "musicId": "G201" },
        { "name": "90s Pop", "musicId": "G202" }
      ]
    },
    {
      "name": "Jazz",
      "genres": [
        { "name": "Smooth Jazz", "musicId": "G300" },
        { "name": "Contemporary Jazz", "musicId": "G301" },
        { "name": "Vocal Jazz", "musicId": "G302" }
      ]
    }
  ]
}
```

---

### `searchResults` - Music Search Results

Sent in response to a `music.search` event. Contains categorized search results for artists and songs.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `categories` | array | Array of result categories |

**Category Object:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | "Artists" or "Songs" |
| `results` | array | Array of result objects |

**Artist Result:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Artist name |
| `musicId` | string | Music ID for station creation or seeding |

**Song Result:**

| Field | Type | Description |
|-------|------|-------------|
| `title` | string | Song title |
| `artist` | string | Artist name |
| `musicId` | string | Music ID for station creation or seeding |

**Example:**

```json
{
  "categories": [
    {
      "name": "Artists",
      "results": [
        { "name": "The Beatles", "musicId": "R12345" },
        { "name": "Beatles Tribute Band", "musicId": "R12346" }
      ]
    },
    {
      "name": "Songs",
      "results": [
        { "title": "Hey Jude", "artist": "The Beatles", "musicId": "S67890" },
        { "title": "Let It Be", "artist": "The Beatles", "musicId": "S67891" },
        { "title": "Yesterday", "artist": "The Beatles", "musicId": "S67892" }
      ]
    }
  ]
}
```

---

### `stationModes` - Station Playback Modes

Sent in response to a `station.getModes` event. Contains available playback modes for a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `modes` | array | Array of mode objects |

**Mode Object:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | number | Mode ID (0-based index) |
| `name` | string | Mode name |
| `description` | string | Mode description |
| `active` | boolean | True if this mode is currently active |

**Example:**

```json
{
  "modes": [
    {
      "id": 0,
      "name": "My Station",
      "description": "Music like the seeds and music you've added",
      "active": true
    },
    {
      "id": 1,
      "name": "Crowd Faves",
      "description": "Music from this station that other people love",
      "active": false
    },
    {
      "id": 2,
      "name": "Deep Cuts",
      "description": "Lesser-known songs from artists in your station",
      "active": false
    },
    {
      "id": 3,
      "name": "Discovery",
      "description": "New releases and new artists related to your station",
      "active": false
    }
  ]
}
```

---

### `stationInfo` - Station Seeds and Feedback

Sent in response to a `station.getInfo` event. Contains the seeds and feedback history for a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `artistSeeds` | array | Artists used as seeds for this station |
| `songSeeds` | array | Songs used as seeds for this station |
| `stationSeeds` | array | Other stations used as seeds |
| `feedback` | array | Thumbs up/down history |

**Artist Seed:**

| Field | Type | Description |
|-------|------|-------------|
| `seedId` | string | Unique seed identifier |
| `name` | string | Artist name |

**Song Seed:**

| Field | Type | Description |
|-------|------|-------------|
| `seedId` | string | Unique seed identifier |
| `title` | string | Song title |
| `artist` | string | Artist name |

**Station Seed:**

| Field | Type | Description |
|-------|------|-------------|
| `seedId` | string | Unique seed identifier |
| `name` | string | Station name |

**Feedback Entry:**

| Field | Type | Description |
|-------|------|-------------|
| `feedbackId` | string | Unique feedback identifier |
| `title` | string | Song title |
| `artist` | string | Artist name |
| `rating` | number | 1 = thumbs up (loved) |

**Example:**

```json
{
  "artistSeeds": [
    { "seedId": "AS1234567890", "name": "The Beatles" },
    { "seedId": "AS0987654321", "name": "The Rolling Stones" }
  ],
  "songSeeds": [
    { "seedId": "SS1122334455", "title": "Imagine", "artist": "John Lennon" }
  ],
  "stationSeeds": [],
  "feedback": [
    { "feedbackId": "F9988776655", "title": "Hey Jude", "artist": "The Beatles", "rating": 1 },
    { "feedbackId": "F5544332211", "title": "Yesterday", "artist": "The Beatles", "rating": 1 }
  ]
}
```

---

## Client-to-Server Events

These events are sent from the client to the server.

### `query` / `query.state` - Request Full State

Request the current application state. Server responds with `process` and `stations` events (unicast to requesting client only).

> **Note:** If the client connected before the Pandora station list was loaded, that first `stations` unicast can be an empty array. The server may then broadcast a full `stations` event to **all** clients as soon as the list is available (e.g. right after startup’s `getStationList`); clients should replace their list on every `stations` they receive.

**Payload:** `null`

**Example:**

```javascript
ws.send('2["query",null]');
```

**Response Events:** `process`, `stations`

---

### `query.stations` - Request Station List

Request just the station list.

**Payload:** `null`

**Example:**

```javascript
ws.send('2["query.stations",null]');
```

**Response Event:** `stations`

---

### `action` - Execute Action Command

Execute a playback, song, or volume action. See [Action Commands](#action-commands) for the complete list.

**Payload:** `string` or `object`

**Simple Action (string):**

```javascript
ws.send('2["action","playback.next"]');
ws.send('2["action","song.love"]');
ws.send('2["action","volume.up"]');
```

**Action with Parameters (object):**

```javascript
// Set volume to 75%
ws.send('2["action",{"action":"volume.set","volume":75}]');

// Alternative format with "command" key
ws.send('2["action",{"command":"playback.pause"}]');
```

---

### `station.change` - Change Station

Switch to a different station.

**Payload:** `string` (station ID) or `object`

**Example (string):**

```javascript
ws.send('2["station.change","3914377188324099182"]');
```

**Example (object):**

```javascript
ws.send('2["station.change",{"id":"3914377188324099182"}]');
```

**Response Events:** `start` (when new song begins), `progress` (during playback)

---

### `station.setQuickMix` - Configure QuickMix Stations

Set which stations are included in the QuickMix.

**Payload:** Array of station IDs (strings)

**Example:**

```javascript
// Include stations 456, 789, and 012 in QuickMix
ws.send('2["station.setQuickMix",["4506787012345678901","5612345678901234567","6789012345678901234"]]');
```

**Response Event:** `stations` (with updated `isQuickMixed` flags)

---

### `station.delete` - Delete Station

Delete a station from the account.

**Payload:** `string` (station ID) or `object`

**Example (string):**

```javascript
ws.send('2["station.delete","3914377188324099182"]');
```

**Example (object):**

```javascript
ws.send('2["station.delete",{"stationId":"3914377188324099182"}]');
```

**Response Event:** `stations` (updated list without deleted station)

**Note:** If the deleted station was currently playing, playback switches to QuickMix.

---

### `station.createFrom` - Create Station from Song/Artist

Create a new station based on the current song or its artist.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `trackToken` | string | Track token from current song |
| `type` | string | `"song"` or `"artist"` |

**Example:**

```javascript
// Create station from current song
ws.send('2["station.createFrom",{"trackToken":"S1234567890abcdef","type":"song"}]');

// Create station from current artist
ws.send('2["station.createFrom",{"trackToken":"S1234567890abcdef","type":"artist"}]');
```

**Response Event:** `stations` (updated list with new station)

---

### `station.getGenres` - Get Genre Categories

Request the list of genre categories for creating genre-based stations.

**Payload:** `{}` (empty object)

**Example:**

```javascript
ws.send('2["station.getGenres",{}]');
```

**Response Event:** `genres`

---

### `station.addGenre` - Create Genre Station

Create a new station from a genre.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `musicId` | string | Music ID from genre list |

**Example:**

```javascript
ws.send('2["station.addGenre",{"musicId":"G100"}]');
```

**Response Event:** `stations` (updated list with new station)

---

### `station.addShared` - Add Shared Station

Add a shared station by its numeric ID (found in Pandora share links).

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `stationId` | string | Numeric station ID (digits only) |

**Example:**

```javascript
// From a share link like pandora.com/station/1234567890
ws.send('2["station.addShared",{"stationId":"1234567890"}]');
```

**Response Event:** `stations` (updated list with new station)

**Note:** The `stationId` must contain only digits.

---

### `music.search` - Search for Music

Search for artists and songs to create stations or add seeds.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `query` | string | Search query |

**Example:**

```javascript
ws.send('2["music.search",{"query":"Beatles"}]');
```

**Response Event:** `searchResults`

---

### `station.addMusic` - Add Music to Station

Add an artist or song as a seed to an existing station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `musicId` | string | Music ID from search results |
| `stationId` | string | Target station ID |

**Example:**

```javascript
ws.send('2["station.addMusic",{"musicId":"R12345","stationId":"3914377188324099182"}]');
```

---

### `station.rename` - Rename Station

Change a station's name.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `stationId` | string | Station ID to rename |
| `newName` | string | New station name |

**Example:**

```javascript
ws.send('2["station.rename",{"stationId":"3914377188324099182","newName":"My Awesome Station"}]');
```

**Response Event:** `stations` (updated list with new name)

---

### `station.getModes` - Get Station Modes

Get available playback modes for a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `stationId` | string | Station ID |

**Example:**

```javascript
ws.send('2["station.getModes",{"stationId":"3914377188324099182"}]');
```

**Response Event:** `stationModes`

**Note:** QuickMix station does not support modes.

---

### `station.setMode` - Set Station Mode

Set the playback mode for a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `stationId` | string | Station ID |
| `modeId` | number | Mode ID from `stationModes` response |

**Example:**

```javascript
// Set to "Deep Cuts" mode
ws.send('2["station.setMode",{"stationId":"3914377188324099182","modeId":2}]');
```

**Note:** Mode change takes effect after current playlist is exhausted.

---

### `station.getInfo` - Get Station Seeds and Feedback

Get the seeds and feedback history for a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `stationId` | string | Station ID |

**Example:**

```javascript
ws.send('2["station.getInfo",{"stationId":"3914377188324099182"}]');
```

**Response Event:** `stationInfo`

---

### `station.deleteSeed` - Delete Station Seed

Remove a seed from a station.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `seedId` | string | Seed ID from `stationInfo` |
| `seedType` | string | `"artist"`, `"song"`, or `"station"` |
| `stationId` | string | Station ID |

**Example:**

```javascript
ws.send('2["station.deleteSeed",{"seedId":"AS1234567890","seedType":"artist","stationId":"3914377188324099182"}]');
```

---

### `station.deleteFeedback` - Delete Station Feedback

Remove a thumbs up/down from a station's history.

**Payload:**

| Field | Type | Description |
|-------|------|-------------|
| `feedbackId` | string | Feedback ID from `stationInfo` |
| `stationId` | string | Station ID |

**Example:**

```javascript
ws.send('2["station.deleteFeedback",{"feedbackId":"F9988776655","stationId":"3914377188324099182"}]');
```

---

## Action Commands

These action strings can be sent via the `action` event.

### Playback Control

| Action | Description |
|--------|-------------|
| `playback.play` | Resume playback |
| `playback.pause` | Pause playback |
| `playback.toggle` | Toggle between play and pause |
| `playback.next` | Skip to next song |

**Examples:**

```javascript
ws.send('2["action","playback.play"]');
ws.send('2["action","playback.pause"]');
ws.send('2["action","playback.toggle"]');
ws.send('2["action","playback.next"]');
```

### Volume Control

| Action | Description |
|--------|-------------|
| `volume.up` | Increase volume by one step |
| `volume.down` | Decrease volume by one step |
| `volume.reset` | Reset volume to 50% (default) |
| `volume.set` | Set specific volume (requires object payload) |

**Examples:**

```javascript
ws.send('2["action","volume.up"]');
ws.send('2["action","volume.down"]');
ws.send('2["action","volume.reset"]');

// Set volume to 75%
ws.send('2["action",{"action":"volume.set","volume":75}]');
```

### Song Actions

| Action | Description | Effect |
|--------|-------------|--------|
| `song.love` | Thumbs up the current song | Song is favorited, more similar songs play |
| `song.ban` | Thumbs down the current song | Song is skipped and won't play on this station |
| `song.tired` | Mark song as "tired" | Song won't play for a while |
| `song.explain` | Get recommendation explanation | Server sends `song.explanation` event |
| `song.info` | Get detailed song info | (Currently same as explain) |
| `song.createStationFrom` | Create station from song | Opens station creation flow |

**Examples:**

```javascript
ws.send('2["action","song.love"]');
ws.send('2["action","song.ban"]');
ws.send('2["action","song.tired"]');
ws.send('2["action","song.explain"]');
```

### Query Actions

| Action | Description |
|--------|-------------|
| `query.upcoming` | Get list of upcoming songs |

**Example:**

```javascript
ws.send('2["action","query.upcoming"]');
```

**Response Event:** `query.upcoming.result`

### Application Actions

| Action | Description |
|--------|-------------|
| `app.quit` | Quit the pianobar application |
| `app.pandora-disconnect` | Stop playback, clear station/playlist, disconnect from Pandora (does not quit) |
| `app.pandora-reconnect` | Re-authenticate with Pandora and fetch stations; optionally switch account |

**Example - Quit:**

```javascript
ws.send('2["action","app.quit"]');
```

**Example - Pandora Disconnect:**

```javascript
ws.send('2["action","app.pandora-disconnect"]');
```

**Response:** Stops playback, clears stations, and disconnects from Pandora. Broadcasts updated state to all clients showing the disconnected state (no station, not playing). The WebSocket connection remains open so the UI can display a "Not Connected" state.

**Example - Pandora Reconnect (current account):**

```javascript
ws.send('2["action","app.pandora-reconnect"]');
```

**Response:** Re-authenticates with Pandora using the current active account's credentials, fetches the station list, and broadcasts `stations` and `process` events to all clients.

**Example - Pandora Reconnect with Account Switch:**

When multiple accounts are configured, pass an `account_id` to switch to a different account before reconnecting.

```javascript
ws.send('2["action",{"action":"app.pandora-reconnect","account_id":"work"}]');
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | yes | Must be `"app.pandora-reconnect"` |
| `account_id` | string | no | Stable account identifier to switch to before reconnecting. Must match an `id` from the `accounts` array in the `process` event. If omitted, reconnects with the current active account. |

**Response:** Sets the active account to the specified `account_id`, stops any current playback, disconnects from the previous Pandora session, re-authenticates with the new account's credentials, fetches the new station list, and broadcasts `stations` and `process` events (including updated `current_account` and `accounts`) to **all** connected clients. If `account_id` is unknown, an error event is emitted.

---

## Data Structures

### Song Object

Used in `process`, `start`, and `query.upcoming.result` events.

```typescript
interface Song {
  title: string;           // Song title
  artist: string;          // Artist name
  album: string;           // Album name
  coverArt: string;        // URL to album artwork
  rating: number;          // 0 = no rating, 1 = loved (thumbs up)
  duration: number;        // Song length in seconds
  trackToken?: string;     // Unique token (for station creation)
  songStationName?: string; // Station the song came from (for QuickMix)
  stationName?: string;    // Alternative field for station name
}
```

### Account Object

Used in the `process` event's `current_account` and `accounts` fields. Only present when multiple accounts are configured.

```typescript
interface Account {
  id: string;              // Stable account identifier (from config, e.g. "work", "default")
  label: string;           // Display name (from account_label in config, or falls back to id)
}
```

### Station Object

Used in `stations` event.

```typescript
interface Station {
  id: string;              // Unique station ID
  name: string;            // Station display name
  isQuickMix: boolean;     // True if this is the QuickMix station
  isQuickMixed: boolean;   // True if included in QuickMix
}
```

### Progress Object

Used in `progress` event.

```typescript
interface Progress {
  elapsed: number;         // Seconds played
  duration: number;        // Total seconds
  percentage: number;      // 0-100 progress
}
```

### Genre Category Object

Used in `genres` event.

```typescript
interface GenreCategory {
  name: string;            // Category name (e.g., "Rock", "Pop")
  genres: Genre[];         // Genres in this category
}

interface Genre {
  name: string;            // Genre name
  musicId: string;         // ID for station.addGenre
}
```

### Search Result Object

Used in `searchResults` event.

```typescript
interface SearchResults {
  categories: SearchCategory[];
}

interface SearchCategory {
  name: string;            // "Artists" or "Songs"
  results: ArtistResult[] | SongResult[];
}

interface ArtistResult {
  name: string;            // Artist name
  musicId: string;         // ID for station creation/seeding
}

interface SongResult {
  title: string;           // Song title
  artist: string;          // Artist name
  musicId: string;         // ID for station creation/seeding
}
```

### Station Mode Object

Used in `stationModes` event.

```typescript
interface StationMode {
  id: number;              // Mode index (0-based)
  name: string;            // Mode name
  description: string;     // Mode description
  active: boolean;         // Currently active mode
}
```

### Station Info Object

Used in `stationInfo` event.

```typescript
interface StationInfo {
  artistSeeds: ArtistSeed[];
  songSeeds: SongSeed[];
  stationSeeds: StationSeed[];
  feedback: FeedbackEntry[];
}

interface ArtistSeed {
  seedId: string;          // For deletion
  name: string;
}

interface SongSeed {
  seedId: string;          // For deletion
  title: string;
  artist: string;
}

interface StationSeed {
  seedId: string;          // For deletion
  name: string;
}

interface FeedbackEntry {
  feedbackId: string;      // For deletion
  title: string;
  artist: string;
  rating: number;          // 1 = thumbs up
}
```

---

## Volume System

The volume API uses a simple 0-100 percentage scale for all communication between client and server.

### Range

- **Minimum:** 0% (silent)
- **Default:** 50%
- **Maximum:** 100%

### Wire Protocol

Both `volume.set` action and `volume` event use 0-100 percentage:

| Value | Description |
|-------|-------------|
| 0 | Silent |
| 25 | Quiet |
| 50 | Default |
| 75 | Loud |
| 100 | Maximum |

### Internal Conversion (Player Mode)

When `volume_mode = player` (the default), the backend converts between percentage and decibels (dB) internally using a perceptual curve. This conversion is handled entirely by the server—clients only need to work with 0-100 percentages.

| Percentage | Internal dB | Description |
|------------|-------------|-------------|
| 0% | -40 dB | Silent |
| 25% | ~-10 dB | Quiet |
| 50% | 0 dB | Unity gain |
| 75% | ~+5 dB | Boosted |
| 100% | +maxGain dB | Maximum boost |

### System Volume Mode

When `volume_mode = system` is configured, the percentage maps directly to the OS system volume (0-100%). No dB conversion is performed.

### Example

```javascript
// Set volume to 75%
ws.send('2["action",{"action":"volume.set","volume":75}]');

// The server will emit a volume event with the percentage
// 75
```

---

## Common Workflows

### Initial Connection

```javascript
const ws = new WebSocket('ws://localhost:8080/socket.io', 'socketio');

let state = {
  playing: false,
  paused: false,
  song: null,
  stations: [],
  currentStation: null
};

ws.onopen = () => {
  // Request full state on connect
  ws.send('2["query",null]');
};

ws.onmessage = (event) => {
  if (!event.data.startsWith('2')) return;
  
  const [eventName, data] = JSON.parse(event.data.substring(1));
  
  switch (eventName) {
    case 'process':
      state.playing = data.playing;
      state.paused = data.paused;
      state.song = data.song;
      state.currentStation = data.station;
      updateUI();
      break;
      
    case 'stations':
      state.stations = data;
      updateStationList();
      break;
      
    case 'start':
      state.playing = true;
      state.paused = false;
      state.song = data;
      updateNowPlaying();
      break;
      
    case 'progress':
      updateProgressBar(data.elapsed, data.duration);
      break;
      
    case 'playState':
      state.paused = data.paused;
      updatePlayPauseButton();
      break;
  }
};
```

### Playing a Station

```javascript
function playStation(stationId) {
  ws.send(`2["station.change","${stationId}"]`);
  // Wait for 'start' event to update UI
}

// Handle station change from the updated station list
ws.onmessage = (event) => {
  // ... parsing code ...
  
  if (eventName === 'start') {
    // New song started on new station
    displaySong(data);
  }
};
```

### Rating Songs

```javascript
function loveSong() {
  ws.send('2["action","song.love"]');
  // Optimistic UI; server confirms with a `process` event (updated song.rating)
  state.song.rating = 1;
  updateLoveButton();
}

function banSong() {
  ws.send('2["action","song.ban"]');
  // `process` updates rating; if skipped, `start` follows for the next track
}

function tiredOfSong() {
  ws.send('2["action","song.tired"]');
  // Song will be skipped, wait for 'start' event
}
```

### Creating a Station from Current Song

```javascript
function createStationFromSong() {
  ws.send(`2["station.createFrom",{"trackToken":"${state.song.trackToken}","type":"song"}]`);
}

function createStationFromArtist() {
  ws.send(`2["station.createFrom",{"trackToken":"${state.song.trackToken}","type":"artist"}]`);
}

// Listen for stations update to find the new station
ws.onmessage = (event) => {
  // ... parsing code ...
  
  if (eventName === 'stations') {
    const newStation = findNewStation(data, state.stations);
    if (newStation) {
      promptPlayNewStation(newStation);
    }
    state.stations = data;
  }
};
```

### Creating a Station from Search

```javascript
// Step 1: Search for music
function searchMusic(query) {
  ws.send(`2["music.search",{"query":"${query}"}]`);
}

// Step 2: Handle search results
ws.onmessage = (event) => {
  if (eventName === 'searchResults') {
    displaySearchResults(data.categories);
  }
};

// Step 3: Create station from selected result
function createStationFromResult(musicId) {
  ws.send(`2["station.addGenre",{"musicId":"${musicId}"}]`);
}
```

### Managing QuickMix

```javascript
// Get stations that are currently in QuickMix
function getQuickMixStations() {
  return state.stations.filter(s => s.isQuickMixed);
}

// Update QuickMix selection
function setQuickMixStations(stationIds) {
  ws.send(`2["station.setQuickMix",${JSON.stringify(stationIds)}]`);
}
```

### Managing Station Seeds

```javascript
// Step 1: Get station info
function getStationInfo(stationId) {
  ws.send(`2["station.getInfo",{"stationId":"${stationId}"}]`);
}

// Step 2: Display seeds and feedback
ws.onmessage = (event) => {
  if (eventName === 'stationInfo') {
    displayArtistSeeds(data.artistSeeds);
    displaySongSeeds(data.songSeeds);
    displayFeedback(data.feedback);
  }
};

// Step 3: Delete a seed
function deleteSeed(seedId, seedType, stationId) {
  ws.send(`2["station.deleteSeed",{"seedId":"${seedId}","seedType":"${seedType}","stationId":"${stationId}"}]`);
}

// Step 4: Delete feedback (thumbs up/down)
function deleteFeedback(feedbackId, stationId) {
  ws.send(`2["station.deleteFeedback",{"feedbackId":"${feedbackId}","stationId":"${stationId}"}]`);
}
```

### Switching Accounts

When the server is configured with multiple Pandora accounts (via `account = id:path` in config), the `process` event includes `accounts` and `current_account` fields. Clients can switch accounts using the `app.pandora-reconnect` action with an `account_id`.

```javascript
// Step 1: Read available accounts from process state
let accounts = [];
let currentAccountId = '';

ws.onmessage = (event) => {
  if (eventName === 'process') {
    if (data.accounts) {
      accounts = data.accounts;            // [{id: "default", label: "Home"}, {id: "work", label: "Work"}]
      currentAccountId = data.current_account?.id || '';
    }
  }
};

// Step 2: Switch to a different account
function switchAccount(accountId) {
  ws.send(`2["action",{"action":"app.pandora-reconnect","account_id":"${accountId}"}]`);
  // Server will disconnect from current Pandora session, re-authenticate
  // with the new account, fetch stations, and broadcast updated state
  // (process + stations events) to ALL connected clients
}
```

After switching, all connected clients receive updated `stations` (new account's station list) and `process` events (with `current_account` reflecting the new account). No polling or manual refresh is needed.

---

## Not Implemented

The following features are defined in the action mappings but are **not implemented** in the WebSocket API:

| Action | Reason |
|--------|--------|
| `song.bookmark` | Pandora discontinued the bookmark feature in August 2022 |
| `query.history` | Not yet implemented |
| `app.settings` | Settings changes don't persist and are only temporary session changes |

Attempting to use these actions will have no effect.

