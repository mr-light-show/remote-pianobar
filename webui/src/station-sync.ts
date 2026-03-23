/** Row shape from `process` / `stations` payloads used to match name → id. */
export interface StationRow {
  id: string;
  name: string;
}

/**
 * When `currentStationId` is empty but `currentStation` matches a station name,
 * returns that station's id. Otherwise returns `undefined` (caller keeps existing id).
 */
export function resolveStationIdFromStationsList(
  currentStationId: string,
  currentStation: string,
  stations: StationRow[]
): string | undefined {
  if ((currentStationId || '').trim() !== '') {
    return undefined;
  }
  const name = (currentStation || '').trim();
  if (!name || stations.length === 0) {
    return undefined;
  }
  const match = stations.find((s) => s.name === name);
  if (match?.id) {
    return String(match.id).trim();
  }
  return undefined;
}
