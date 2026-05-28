/**
 * Typed contract for the Socket.IO wire protocol between server and web UI.
 * All server-to-client event payloads are defined here.
 * Source of truth: WEBSOCKET_API.md
 */

export interface StationPayload {
  id: string;
  name: string;
  isQuickMix?: boolean;
  isQuickMixed?: boolean;
}

export interface SongPayload {
  title?: string;
  artist?: string;
  album?: string;
  coverArt?: string;
  duration?: number;
  rating?: number;
  trackToken?: string;
  stationId?: string;
  /** Station name as set at song-start time (sent on the `start` event). */
  station?: string;
  songStationName?: string;
}

export interface AccountPayload {
  id: string;
  label?: string;
}

export interface ProcessPayload {
  song?: SongPayload;
  station?: string;
  stationId?: string;
  playing?: boolean;
  paused?: boolean;
  volume?: number;
  elapsed?: number;
  current_account?: AccountPayload;
  accounts?: AccountPayload[];
}

export interface ProgressPayload {
  elapsed: number;
  duration: number;
  percentage: number;
}

export interface PlayStatePayload {
  paused: boolean;
}

export interface ErrorPayload {
  operation?: string;
  message?: string;
  stationId?: string;
}

export interface DisconnectedPayload {
  reason: string;
}

export interface SongExplanationPayload {
  explanation?: string;
}

/**
 * Volume event payload — server sends a bare number (0–100).
 * Legacy clients also observed an object shape; the union preserves compat.
 */
export type VolumePayload = number | { volume: number };

/** Map of event name → payload type for all server-to-client events */
export interface ServerEvents {
  start: SongPayload;
  stop: undefined;
  volume: VolumePayload;
  progress: ProgressPayload;
  stations: StationPayload[];
  process: ProcessPayload;
  playState: PlayStatePayload;
  error: ErrorPayload;
  'pandora.disconnected': DisconnectedPayload;
  'song.explanation': SongExplanationPayload;
  'query.upcoming.result': SongPayload[];
  genres: { categories: unknown[] };
  /** `searchResults` is the event name the server uses for music/genre search results. */
  searchResults: { categories: unknown[] };
  /** `stationModes` is the event name the server uses for station mode lists. */
  stationModes: { modes: unknown[] };
  /** `stationInfo` is the event name the server uses for station seed info. */
  stationInfo: unknown;
  'query.stations': StationPayload[];
}
