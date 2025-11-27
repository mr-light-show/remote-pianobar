import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/stations-menu';
import type { StationsMenu } from '../../src/components/stations-menu';

describe('StationsMenu', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station' },
    { id: '2', name: 'Jazz Station' },
    { id: '3', name: 'Classical Station' },
  ];

  describe('rendering', () => {
    it('renders all stations as buttons', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons.length).toBe(3);
    });

    it('renders station names', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons[0].textContent?.trim()).toBe('Rock Station');
      expect(buttons[1].textContent?.trim()).toBe('Jazz Station');
      expect(buttons[2].textContent?.trim()).toBe('Classical Station');
    });

    it('renders empty when no stations provided', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${[]}></stations-menu>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons.length).toBe(0);
    });

    it('highlights current station', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu 
          .stations=${mockStations}
          currentStation="2"
        ></stations-menu>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons[0].classList.contains('active')).toBe(false);
      expect(buttons[1].classList.contains('active')).toBe(true);
      expect(buttons[2].classList.contains('active')).toBe(false);
    });

    it('does not highlight any station when currentStation is not set', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const activeButtons = Array.from(buttons).filter(btn => 
        btn.classList.contains('active')
      );

      expect(activeButtons.length).toBe(0);
    });
  });

  describe('station selection', () => {
    it('dispatches station-change event when station is clicked', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('station-change', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      buttons[0].click();

      expect(eventFired).toBe(true);
      expect(eventDetail.station).toBe('1');
    });

    it('dispatches event with correct station ID for each station', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      let eventDetail: any;
      element.addEventListener('station-change', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const buttons = element.shadowRoot!.querySelectorAll('button');

      buttons[0].click();
      expect(eventDetail.station).toBe('1');

      buttons[1].click();
      expect(eventDetail.station).toBe('2');

      buttons[2].click();
      expect(eventDetail.station).toBe('3');
    });
  });

  describe('styling', () => {
    it('renders buttons with proper structure', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      expect(button).toBeTruthy();
      expect(button.tagName).toBe('BUTTON');
    });

    it('shows border on buttons', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      const styles = window.getComputedStyle(button);
      
      expect(styles.border).toBeTruthy();
    });
  });

  describe('dynamic updates', () => {
    it('updates when stations prop changes', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu .stations=${mockStations}></stations-menu>
      `);

      let buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons.length).toBe(3);

      const newStations = [
        { id: '4', name: 'Pop Station' },
        { id: '5', name: 'Country Station' },
      ];
      element.stations = newStations;
      await element.updateComplete;

      buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons.length).toBe(2);
      expect(buttons[0].textContent?.trim()).toBe('Pop Station');
      expect(buttons[1].textContent?.trim()).toBe('Country Station');
    });

    it('updates highlighting when currentStation changes', async () => {
      const element: StationsMenu = await fixture(html`
        <stations-menu 
          .stations=${mockStations}
          currentStation="1"
        ></stations-menu>
      `);

      let buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons[0].classList.contains('active')).toBe(true);

      element.currentStation = '2';
      await element.updateComplete;

      buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons[0].classList.contains('active')).toBe(false);
      expect(buttons[1].classList.contains('active')).toBe(true);
    });
  });
});

