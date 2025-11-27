import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/bottom-toolbar';
import type { BottomToolbar } from '../../src/components/bottom-toolbar';

describe('BottomToolbar', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false },
    { id: '2', name: 'Jazz Station', isQuickMix: false },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true },
  ];

  describe('rendering', () => {
    it('renders station button with current station name', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="Rock Station"
          currentStationId="1"
        ></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button).toBeTruthy();
      expect(button!.textContent).toContain('Rock Station');
    });

    it('displays "Select Station" when no station is selected', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar .stations=${mockStations}></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button!.textContent).toContain('Select Station');
    });

    it('displays shuffle icon when current station is QuickMix', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="QuickMix"
          currentStationId="quickmix"
        ></bottom-toolbar>
      `);

      const icons = element.shadowRoot!.querySelectorAll('.material-icons');
      const shuffleIcon = Array.from(icons).find(icon => 
        icon.textContent?.includes('shuffle')
      );
      expect(shuffleIcon).toBeTruthy();
    });

    it('does not display shuffle icon for non-QuickMix stations', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="Rock Station"
          currentStationId="1"
        ></bottom-toolbar>
      `);

      const icons = element.shadowRoot!.querySelectorAll('.material-icons');
      const shuffleIcon = Array.from(icons).find(icon => 
        icon.textContent?.includes('shuffle')
      );
      expect(shuffleIcon).toBeFalsy();
    });

    it('displays song station name when playing from QuickMix', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="QuickMix"
          currentStationId="quickmix"
          songStationName="Jazz Station"
        ></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button!.textContent).toContain('Jazz Station');
      expect(button!.textContent).not.toContain('QuickMix');
    });

    it('renders stations-popup component', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar .stations=${mockStations}></bottom-toolbar>
      `);

      const popup = element.shadowRoot!.querySelector('stations-popup');
      expect(popup).toBeTruthy();
    });

    it('passes stations prop to stations-popup', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar .stations=${mockStations}></bottom-toolbar>
      `);

      const popup = element.shadowRoot!.querySelector('stations-popup') as any;
      expect(popup.stations).toEqual(mockStations);
    });

    it('passes current station info to stations-popup', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="Rock Station"
          currentStationId="1"
        ></bottom-toolbar>
      `);

      const popup = element.shadowRoot!.querySelector('stations-popup') as any;
      expect(popup.currentStation).toBe('Rock Station');
      expect(popup.currentStationId).toBe('1');
    });
  });

  describe('interactions', () => {
    it('toggles stations menu when button is clicked', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar .stations=${mockStations}></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      const popup = element.shadowRoot!.querySelector('stations-popup') as any;

      // Spy on the popup's toggleMenu method
      let toggleCalled = false;
      popup.toggleMenu = () => { toggleCalled = true; };

      button.click();

      expect(toggleCalled).toBe(true);
    });
  });

  describe('events', () => {
    it('propagates station-change event from popup', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar .stations=${mockStations}></bottom-toolbar>
      `);

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('station-change', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const popup = element.shadowRoot!.querySelector('stations-popup');
      popup!.dispatchEvent(new CustomEvent('station-change', {
        detail: { station: '2' },
        bubbles: true,
        composed: true
      }));

      expect(eventFired).toBe(true);
      expect(eventDetail).toEqual({ station: '2' });
    });
  });

  describe('QuickMix display logic', () => {
    it('shows QuickMix name when no song is playing', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="QuickMix"
          currentStationId="quickmix"
          songStationName=""
        ></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button!.textContent).toContain('QuickMix');
    });

    it('shows song station name when song is playing from QuickMix', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="QuickMix"
          currentStationId="quickmix"
          songStationName="Rock Station"
        ></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button!.textContent).toContain('Rock Station');
    });

    it('ignores songStationName for non-QuickMix stations', async () => {
      const element: BottomToolbar = await fixture(html`
        <bottom-toolbar 
          .stations=${mockStations}
          currentStation="Jazz Station"
          currentStationId="2"
          songStationName="Rock Station"
        ></bottom-toolbar>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button!.textContent).toContain('Jazz Station');
      expect(button!.textContent).not.toContain('Rock Station');
    });
  });
});

