import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/play-station-modal';
import type { PlayStationModal } from '../../src/components/play-station-modal';

describe('PlayStationModal', () => {
  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Station Created!');
    });

    it('renders station name in message', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal 
          open 
          stationName="Rock Station"
        ></play-station-modal>
      `);

      const stationName = element.shadowRoot!.querySelector('.station-name');
      expect(stationName).toBeTruthy();
      expect(stationName!.textContent).toBe('Rock Station');
    });

    it('renders confirmation message', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal 
          open 
          stationName="Rock Station"
        ></play-station-modal>
      `);

      const message = element.shadowRoot!.querySelector('.message');
      expect(message).toBeTruthy();
      expect(message!.textContent).toContain('Play');
      expect(message!.textContent).toContain('now?');
    });

    it('renders Later button', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const laterButton = Array.from(buttons).find(btn =>
        btn.textContent?.trim() === 'Later'
      );

      expect(laterButton).toBeTruthy();
      expect(laterButton!.classList.contains('button-cancel')).toBe(true);
    });

    it('renders Play Now button', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const playButton = Array.from(buttons).find(btn =>
        btn.textContent?.trim() === 'Play Now'
      );

      expect(playButton).toBeTruthy();
      expect(playButton!.classList.contains('button-confirm')).toBe(true);
    });

    it('renders both buttons in buttons container', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      const buttonsContainer = element.shadowRoot!.querySelector('.buttons');
      expect(buttonsContainer).toBeTruthy();

      const buttons = buttonsContainer!.querySelectorAll('button');
      expect(buttons.length).toBe(2);
    });
  });

  describe('button interactions', () => {
    it('dispatches play event when Play Now is clicked', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      let eventFired = false;
      element.addEventListener('play', () => {
        eventFired = true;
      });

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const playButton = Array.from(buttons).find(btn =>
        btn.textContent?.trim() === 'Play Now'
      ) as HTMLButtonElement;

      playButton.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches cancel event when Later is clicked', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      let eventFired = false;
      element.addEventListener('cancel', () => {
        eventFired = true;
      });

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const laterButton = Array.from(buttons).find(btn =>
        btn.textContent?.trim() === 'Later'
      ) as HTMLButtonElement;

      laterButton.click();

      expect(eventFired).toBe(true);
    });
  });

  describe('styling', () => {
    it('applies station-name class for emphasis', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal 
          open 
          stationName="Rock Station"
        ></play-station-modal>
      `);

      const stationName = element.shadowRoot!.querySelector('.station-name');
      expect(stationName).toBeTruthy();

      // Verify it has the expected class for styling
      const computedStyle = window.getComputedStyle(stationName as Element);
      expect(computedStyle).toBeTruthy();
    });

    it('uses modal body layout', async () => {
      const element: PlayStationModal = await fixture(html`
        <play-station-modal open></play-station-modal>
      `);

      const modalBody = element.shadowRoot!.querySelector('.modal-body');
      expect(modalBody).toBeTruthy();
    });
  });
});

