import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/stations-popup';
import type { StationsPopup } from '../../src/components/stations-popup';

describe('StationsPopup', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false, isQuickMixed: false },
    { id: '2', name: 'Jazz Station', isQuickMix: false, isQuickMixed: true },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true, isQuickMixed: false },
  ];

  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  describe('rendering', () => {
    it('starts with menu closed', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('renders all stations', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      expect(stationButtons.length).toBe(3);
    });

    it('renders station names', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Rock Station');
      expect(content!.textContent).toContain('Jazz Station');
      expect(content!.textContent).toContain('QuickMix');
    });

    it('shows shuffle icon for QuickMix station', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const quickmixButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('QuickMix')
      );

      const leadingIcon = quickmixButton!.querySelector('.station-icon-leading');
      expect(leadingIcon!.textContent).toContain('shuffle');
    });

    it('shows play_arrow icon for regular stations', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const rockButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Rock Station')
      );

      const leadingIcon = rockButton!.querySelector('.station-icon-leading');
      expect(leadingIcon!.textContent).toContain('play_arrow');
    });

    it('shows trailing shuffle icon for stations in QuickMix', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const jazzButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Jazz Station')
      );

      const trailingIcon = jazzButton!.querySelector('.quickmix-icon');
      expect(trailingIcon).toBeTruthy();
      expect(trailingIcon!.textContent).toContain('shuffle');
    });

    it('does not show trailing shuffle icon for stations not in QuickMix', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const rockButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Rock Station')
      );

      const trailingIcon = rockButton!.querySelector('.quickmix-icon');
      expect(trailingIcon).toBeFalsy();
    });

    it('highlights current station by ID', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup 
          .stations=${mockStations}
          currentStationId="2"
        ></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const jazzButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Jazz Station')
      );

      expect(jazzButton!.classList.contains('active')).toBe(true);
    });

    it('does not highlight other stations', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup 
          .stations=${mockStations}
          currentStationId="2"
        ></stations-popup>
      `);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const rockButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Rock Station')
      );

      expect(rockButton!.classList.contains('active')).toBe(false);
    });
  });

  describe('menu toggle', () => {
    it('uses setTimeout(0) pattern for toggle', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      element.toggleMenu();
      
      // Menu should not open immediately
      let menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);

      // Advance timers
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Menu should now be open
      menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(false);
    });

    it('toggles menu open and closed', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      // Open
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(false);

      // Close
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });
  });

  describe('outside click dismissal', () => {
    it('adds document click listener on connect', async () => {
      const addEventListenerSpy = vi.spyOn(document, 'addEventListener');

      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      expect(addEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      addEventListenerSpy.mockRestore();
    });

    it('removes document click listener on disconnect', async () => {
      const removeEventListenerSpy = vi.spyOn(document, 'removeEventListener');

      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      element.remove();

      expect(removeEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      removeEventListenerSpy.mockRestore();
    });

    it('closes menu on outside click', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      // Open menu
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Click outside
      document.body.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('does not close menu on inside click', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      // Open menu
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Click inside
      const menuPopup = element.shadowRoot!.querySelector('.menu-popup') as HTMLElement;
      menuPopup.click();
      await element.updateComplete;

      expect(menuPopup.classList.contains('hidden')).toBe(false);
    });
  });

  describe('station selection', () => {
    it('dispatches station-change event when station is clicked', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('station-change', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const rockButton = stationButtons[0] as HTMLButtonElement;
      rockButton.click();

      expect(eventDetail.station).toBe('1');
    });

    it('closes menu after station is selected', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      const rockButton = stationButtons[0] as HTMLButtonElement;
      rockButton.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('dispatches event with correct station ID for each station', async () => {
      const element: StationsPopup = await fixture(html`
        <stations-popup .stations=${mockStations}></stations-popup>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('station-change', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const stationButtons = element.shadowRoot!.querySelectorAll('.station-button');
      
      // Click Jazz Station
      const jazzButton = Array.from(stationButtons).find(btn =>
        btn.textContent?.includes('Jazz Station')
      ) as HTMLButtonElement;
      jazzButton.click();

      expect(eventDetail.station).toBe('2');
    });
  });
});

