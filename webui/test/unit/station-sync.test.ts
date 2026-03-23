import { describe, it, expect } from 'vitest';
import { resolveStationIdFromStationsList } from '../../src/station-sync';

describe('resolveStationIdFromStationsList', () => {
  const stations = [
    { id: 'S1', name: 'Rock' },
    { id: 'S2', name: 'Jazz' },
  ];

  it('returns undefined when station id is already set', () => {
    expect(
      resolveStationIdFromStationsList('S9', 'Rock', stations)
    ).toBeUndefined();
  });

  it('returns undefined when station name is empty', () => {
    expect(resolveStationIdFromStationsList('', '', stations)).toBeUndefined();
  });

  it('returns undefined when stations list is empty', () => {
    expect(resolveStationIdFromStationsList('', 'Rock', [])).toBeUndefined();
  });

  it('resolves id from matching station name', () => {
    expect(resolveStationIdFromStationsList('', 'Jazz', stations)).toBe('S2');
  });

  it('trims whitespace on ids and names', () => {
    expect(
      resolveStationIdFromStationsList('  ', '  Rock  ', stations)
    ).toBe('S1');
  });

  it('returns undefined when no name match', () => {
    expect(
      resolveStationIdFromStationsList('', 'Classical', stations)
    ).toBeUndefined();
  });
});
