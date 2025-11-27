import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/station-mode-modal';
import type { StationModeModal } from '../../src/components/station-mode-modal';

describe('StationModeModal', () => {
  const mockModes = [
    { id: 0, name: 'Standard', description: 'Mix of familiar and new music', active: true },
    { id: 1, name: 'Discovery', description: 'More variety and new music', active: false },
    { id: 2, name: 'Deep Cuts', description: 'Deeper album tracks', active: false },
  ];

  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Manage Station Mode');
    });

    it('updates title with station name', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal 
          open
          .modes=${mockModes}
          currentStationName="Rock Station"
        ></station-mode-modal>
      `);

      await element.updateComplete;

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Station Mode: Rock Station');
    });

    it('renders info note about playback restart', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const infoNote = element.shadowRoot!.querySelector('.info-note');
      expect(infoNote).toBeTruthy();
      expect(infoNote!.textContent).toContain('restart playback');
    });

    it('shows loading state when modesLoading is true', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open modesLoading></station-mode-modal>
      `);

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Loading modes...');
    });

    it('renders all mode options', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      expect(modeItems.length).toBe(3);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Standard');
      expect(content!.textContent).toContain('Discovery');
      expect(content!.textContent).toContain('Deep Cuts');
    });

    it('renders mode names', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeNames = element.shadowRoot!.querySelectorAll('.mode-name');
      expect(modeNames.length).toBe(3);
      expect(modeNames[0].textContent).toBe('Standard');
      expect(modeNames[1].textContent).toBe('Discovery');
      expect(modeNames[2].textContent).toBe('Deep Cuts');
    });

    it('renders mode descriptions', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeDescriptions = element.shadowRoot!.querySelectorAll('.mode-description');
      expect(modeDescriptions.length).toBe(3);
      expect(modeDescriptions[0].textContent).toBe('Mix of familiar and new music');
      expect(modeDescriptions[1].textContent).toBe('More variety and new music');
      expect(modeDescriptions[2].textContent).toBe('Deeper album tracks');
    });

    it('shows active badge for active mode', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const standardMode = modeItems[0];
      
      expect(standardMode.classList.contains('active')).toBe(true);
      
      const activeBadge = standardMode.querySelector('.mode-active-badge');
      expect(activeBadge).toBeTruthy();
      expect(activeBadge!.textContent).toBe('ACTIVE');
    });

    it('does not show active badge for inactive modes', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1];
      
      const activeBadge = discoveryMode.querySelector('.mode-active-badge');
      expect(activeBadge).toBeFalsy();
    });

    it('renders radio buttons for each mode', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      expect(radios.length).toBe(3);
    });
  });

  describe('mode selection', () => {
    it('starts with no mode selected', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(0);
    });

    it('selects mode when radio button is clicked', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const discoveryRadio = radios[1] as HTMLInputElement;
      
      discoveryRadio.click();
      await element.updateComplete;

      expect(discoveryRadio.checked).toBe(true);
    });

    it('selects mode when mode item is clicked', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1] as HTMLElement;
      
      discoveryMode.click();
      await element.updateComplete;

      const radio = discoveryMode.querySelector('input[type="radio"]') as HTMLInputElement;
      expect(radio.checked).toBe(true);
    });

    it('adds selected class to selected mode item', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1] as HTMLElement;
      
      discoveryMode.click();
      await element.updateComplete;

      expect(discoveryMode.classList.contains('selected')).toBe(true);
    });

    it('allows only one mode to be selected', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const standardMode = modeItems[0] as HTMLElement;
      const discoveryMode = modeItems[1] as HTMLElement;
      
      standardMode.click();
      await element.updateComplete;
      
      discoveryMode.click();
      await element.updateComplete;

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(1);
      expect((radios[1] as HTMLInputElement).checked).toBe(true);
    });
  });

  describe('set mode functionality', () => {
    it('disables Set Mode button when no mode selected', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal 
          open
          .modes=${mockModes}
          currentStationId="station1"
        ></station-mode-modal>
      `);

      const setModeButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(setModeButton.disabled).toBe(true);
    });

    it('enables Set Mode button when mode is selected', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal 
          open
          .modes=${mockModes}
          currentStationId="station1"
        ></station-mode-modal>
      `);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1] as HTMLElement;
      discoveryMode.click();
      await element.updateComplete;

      const setModeButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(setModeButton.disabled).toBe(false);
    });

    it('disables Set Mode button during loading', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal 
          open
          .modes=${mockModes}
          currentStationId="station1"
          modesLoading
        ></station-mode-modal>
      `);

      const setModeButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(setModeButton.disabled).toBe(true);
    });

    it('dispatches set-mode event with stationId and modeId', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal 
          open
          .modes=${mockModes}
          currentStationId="station1"
        ></station-mode-modal>
      `);

      let eventDetail: any;
      element.addEventListener('set-mode', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1] as HTMLElement;
      discoveryMode.click();
      await element.updateComplete;

      const setModeButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      setModeButton.click();

      expect(eventDetail.stationId).toBe('station1');
      expect(eventDetail.modeId).toBe(1);
    });
  });

  describe('cancel handling', () => {
    it('resets selection on cancel', async () => {
      const element: StationModeModal = await fixture(html`
        <station-mode-modal open .modes=${mockModes}></station-mode-modal>
      `);

      // Select a mode
      const modeItems = element.shadowRoot!.querySelectorAll('.mode-item');
      const discoveryMode = modeItems[1] as HTMLElement;
      discoveryMode.click();
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(0);
    });
  });
});

