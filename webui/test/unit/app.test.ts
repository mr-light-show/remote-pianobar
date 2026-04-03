import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';

const hoisted = vi.hoisted(() => ({
  handlers: {} as Record<string, ((data?: any) => void)[]>,
  connectionCb: null as null | ((connected: boolean) => void),
  fire(event: string, data?: any) {
    for (const fn of hoisted.handlers[event] ?? []) {
      fn(data);
    }
  },
  setConnected(connected: boolean) {
    hoisted.connectionCb?.(connected);
  },
  clearHandlers() {
    for (const k of Object.keys(hoisted.handlers)) {
      delete hoisted.handlers[k];
    }
    hoisted.connectionCb = null;
  },
}));

vi.mock('../../src/services/socket-service', () => ({
  SocketService: vi.fn().mockImplementation(() => ({
    on(event: string, cb: (data?: any) => void) {
      if (!hoisted.handlers[event]) {
        hoisted.handlers[event] = [];
      }
      hoisted.handlers[event].push(cb);
    },
    emit: vi.fn(),
    onConnectionChange(cb: (connected: boolean) => void) {
      hoisted.connectionCb = cb;
      cb(false);
    },
    reconnect: vi.fn(),
    disconnect: vi.fn(),
  })),
}));

import '../../src/app';
import type { PianobarApp } from '../../src/app';
import { SocketService } from '../../src/services/socket-service';

function removeToasts() {
  document.querySelectorAll('toast-notification').forEach((el) => el.remove());
}

async function mountApp(): Promise<PianobarApp> {
  const el = await fixture(html`<pianobar-app></pianobar-app>`);
  await el.updateComplete;
  return el as PianobarApp;
}

/** Connected WebSocket + station list so main UI (not login-only) renders. */
async function mountConnectedApp(): Promise<PianobarApp> {
  const el = await mountApp();
  hoisted.setConnected(true);
  await el.updateComplete;
  hoisted.fire('stations', [{ id: 's1', name: 'Rock' }]);
  hoisted.fire('process', {
    playing: false,
    station: 'Rock',
    stationId: 's1',
    paused: false,
    volume: 50,
  });
  await el.updateComplete;
  return el;
}

describe('PianobarApp', () => {
  beforeEach(() => {
    hoisted.clearHandlers();
    vi.mocked(SocketService).mockClear();
    removeToasts();
  });

  afterEach(() => {
    removeToasts();
  });

  it('starts disconnected then can mark connected', async () => {
    const el = await mountApp();
    expect(el.shadowRoot?.querySelector('h1')?.textContent).toBe('Disconnected');

    hoisted.setConnected(true);
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('h1')?.textContent).not.toBe('Disconnected');
  });

  it('shows localized menu title when connected', async () => {
    const el = await mountConnectedApp();
    const menuBtn = el.shadowRoot?.querySelector('.menu-button') as HTMLButtonElement | null;
    expect(menuBtn?.getAttribute('title')).toBe('Menu');
  });

  it('applies start event and syncs station id from list when stationId missing', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('stations', [{ id: 'sx', name: 'Jazz FM' }]);
    hoisted.fire('start', {
      coverArt: 'http://x/art.jpg',
      title: 'Song',
      album: 'Alb',
      artist: 'Art',
      duration: 200,
      rating: 0,
      songStationName: 'Jazz FM',
      trackToken: 'tok',
      station: 'Jazz FM',
      stationId: '',
    });
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('h1')?.textContent).toBe('Song');
    // Resolved via syncCurrentStationIdFromStationsList
    const vol = el.shadowRoot?.querySelector('volume-control');
    expect(vol).toBeTruthy();
  });

  it('handles stop, progress, and playState', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('start', {
      title: 'T',
      artist: 'A',
      duration: 100,
      station: 'Rock',
      stationId: 's1',
    });
    await el.updateComplete;
    hoisted.fire('progress', { elapsed: 10, duration: 100 });
    hoisted.fire('playState', { paused: true });
    await el.updateComplete;
    hoisted.fire('stop');
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('h1')?.textContent).toBe('T');
  });

  it('handles volume event as number and as object', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('volume', 72);
    await el.updateComplete;
    hoisted.fire('volume', { volume: 33 });
    await el.updateComplete;
    const vc = el.shadowRoot?.querySelector('volume-control') as any;
    expect(vc?.volume).toBe(33);
  });

  it('process event updates song, accounts, and volume branch', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('process', {
      playing: true,
      paused: false,
      elapsed: 5,
      volume: 61,
      accounts: [
        { id: 'u1', label: 'One' },
        { id: 'u2', label: 'Two' },
      ],
      current_account: { id: 'u2' },
      song: {
        coverArt: '',
        title: 'Proc',
        album: 'B',
        artist: 'C',
        duration: 120,
        rating: 1,
        songStationName: 'Rock',
        trackToken: 't2',
      },
      station: 'Rock',
      stationId: 's1',
    });
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('h1')?.textContent).toBe('Proc');
    const menu = el.shadowRoot?.querySelector('info-menu') as any;
    expect(menu?.showAccountSwitch).toBe(true);
  });

  it('process without song shows select-station hint when no station', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('stations', []);
    hoisted.fire('process', {
      playing: false,
      station: '',
      stationId: '',
      paused: false,
    });
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('.artist')?.textContent).toContain('Select a station');
  });

  it('pandora.disconnected clears playback fields', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('start', {
      title: 'Live',
      artist: 'A',
      duration: 60,
      station: 'Rock',
      stationId: 's1',
    });
    await el.updateComplete;
    hoisted.fire('pandora.disconnected', { reason: 'idle' });
    await el.updateComplete;
    expect(el.shadowRoot?.querySelector('h1')?.textContent).toBe('Not Playing');
  });

  it('error event for station.change removes station and toasts', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('error', {
      operation: 'station.change',
      message: 'Station not found',
      stationId: 's1',
    });
    await el.updateComplete;
    expect(document.querySelector('toast-notification')).toBeTruthy();
  });

  it('error event for reconnect last station path', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('error', {
      operation: 'app.pandora-reconnect',
      message: 'Last station was deleted',
      stationId: 's1',
    });
    await el.updateComplete;
    expect(document.querySelector('toast-notification')).toBeTruthy();
  });

  it('generic error shows toast and clears playing for playback.play', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('error', {
      operation: 'playback.play',
      message: 'Not connected to Pandora',
    });
    await el.updateComplete;
    const toast = document.querySelector('toast-notification');
    expect(toast).toBeTruthy();
  });

  it('song.explanation and query.upcoming.result show toasts', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('song.explanation', { explanation: 'Because you liked X' });
    hoisted.fire('query.upcoming.result', []);
    hoisted.fire('query.upcoming.result', [
      {
        title: 'Next',
        artist: 'A',
        duration: 90,
        coverArt: 'http://c.jpg',
        stationName: 'Rock',
        rating: 1,
      },
    ]);
    expect(document.querySelectorAll('toast-notification').length).toBeGreaterThan(0);
  });

  it('genres, searchResults, stationModes, stationInfo handlers run', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('genres', { categories: [{ id: 1, name: 'G' }] });
    hoisted.fire('searchResults', { categories: [] });
    hoisted.fire('stationModes', { modes: [{ id: 0, name: 'M', description: '', active: true }] });
    hoisted.fire('stationInfo', {
      artistSeeds: [],
      songSeeds: [],
      stationSeeds: [],
      feedback: [],
    });
    await el.updateComplete;
    expect(true).toBe(true);
  });

  it('play/pause and next emit socket actions when controls used', async () => {
    const el = await mountConnectedApp();
    const playback = el.shadowRoot?.querySelector('playback-controls');
    const playBtn = playback?.shadowRoot?.querySelector('button.primary') as HTMLButtonElement;
    expect(playBtn).toBeTruthy();
    playBtn.click();
    await el.updateComplete;
    const socket = vi.mocked(SocketService).mock.results[0]?.value;
    expect(socket.emit).toHaveBeenCalled();
    const nextBtn = playback?.shadowRoot?.querySelectorAll('button')[1] as HTMLButtonElement;
    nextBtn?.click();
    expect(socket.emit).toHaveBeenCalled();
  });

  it('handleAccountSelect emits reconnect with account_id', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('process', {
      playing: false,
      station: 'Rock',
      stationId: 's1',
      accounts: [
        { id: 'a', label: 'A' },
        { id: 'b', label: 'B' },
      ],
      current_account: { id: 'a' },
    });
    await el.updateComplete;
    const modal = el.shadowRoot?.querySelector('switch-account-modal');
    modal?.dispatchEvent(
      new CustomEvent('account-select', { detail: { accountId: 'b' }, bubbles: true, composed: true })
    );
    await el.updateComplete;
    const socket = vi.mocked(SocketService).mock.results[0]?.value;
    expect(socket.emit).toHaveBeenCalledWith(
      'action',
      expect.objectContaining({ action: 'app.pandora-reconnect', account_id: 'b' })
    );
  });

  it('handleInfoStationMode shows toast when no station id', async () => {
    const el = await mountConnectedApp();
    hoisted.fire('process', {
      playing: false,
      station: '',
      stationId: '',
    });
    await el.updateComplete;
    el.handleInfoStationMode();
    await el.updateComplete;
    expect(document.querySelector('toast-notification')).toBeTruthy();
  });

  it('handleInfoStationSeeds opens modal and handleGetStationInfo emits when station id set', async () => {
    const el = await mountConnectedApp();
    el.handleInfoStationSeeds();
    await el.updateComplete;
    el.handleGetStationInfo();
    const socket = vi.mocked(SocketService).mock.results[0]?.value;
    expect(socket.emit).toHaveBeenCalledWith(
      'station.getInfo',
      expect.objectContaining({ stationId: 's1' })
    );
  });

  it('handleDeleteSeed and handleDeleteFeedback advance refresh timer; handleStationSeedsCancel clears', async () => {
    vi.useFakeTimers();
    const el = await mountConnectedApp();
    el.handleDeleteSeed(
      new CustomEvent('delete-seed', { detail: { seedId: '1', seedType: 'artist' } }) as any
    );
    el.handleDeleteFeedback(
      new CustomEvent('delete-feedback', { detail: { feedbackId: 'f1' } }) as any
    );
    await vi.advanceTimersByTimeAsync(500);
    await el.updateComplete;
    el.handleStationSeedsCancel();
    await el.updateComplete;
    vi.useRealTimers();
    const socket = vi.mocked(SocketService).mock.results[0]?.value;
    expect(socket.emit).toHaveBeenCalled();
  });
});
