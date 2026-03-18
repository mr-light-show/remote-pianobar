/*
 * Named constants for timeouts, buffer sizes, and percentages.
 * Single place to change key literals used across the codebase.
 */
#ifndef BAR_CONSTANTS_H
#define BAR_CONSTANTS_H

/* --- WebSocket --- */
#define WEBSOCKET_PING_TIMEOUT_SEC    25   /* Send PING after this many seconds idle */
#define WEBSOCKET_HANGUP_TIMEOUT_SEC  60   /* Hang up if no valid PING response within this */
#define WEBSOCKET_POLL_MS             50   /* lws_service() poll interval (ms) */
#define LWS_RX_BUFFER_SIZE            4096 /* Per-protocol receive buffer size (libwebsockets) */
#define WEBSOCKET_FILEPATH_MAX        512  /* Max filepath length for webui / static files */
#define LOG_MESSAGE_TRUNCATE_LEN      100  /* Truncate long messages in log output to this many chars */

/* --- Volume --- */
#define VOLUME_FALLBACK_PERCENT       50   /* Use when OS/volume read fails (0-100 scale) */
#define DEFAULT_VOLUME_PERCENT        50   /* Default volume at startup / reset (0-100 scale) */
#define VOLUME_MAX_PERCENT            100  /* Upper bound for volume clamp (0-100 scale) */

/* --- Buffer sizes (shared across files) --- */
#define BAR_BUF_SMALL                256  /* Small buffers (lines, error messages) */
#define BAR_BUF_MEDIUM               512  /* Medium buffers (paths, config lines, outstr) */
#define BAR_BUF_LARGE                2048 /* Large buffers (URLs) */
#define BAR_INPUT_MAX                100  /* Credential / short input line size (name, password, lineBuf) */

/* --- System / platform --- */
#define BAR_PA_READY_TRIES           50   /* Max PulseAudio connection wait iterations */
#define BAR_PA_OP_MAX_ITERATIONS     100  /* Max PulseAudio operation wait iterations (~1s) */
#define BAR_JOIN_THREAD_ITERATIONS   100  /* Main: wait iterations for player thread exit (100 * 100ms = 10s) */
#define BAR_STATION_ID_MAX           50   /* Max length for station ID string buffer */

#endif /* BAR_CONSTANTS_H */
