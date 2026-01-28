import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/rename-station-modal';
import type { RenameStationModal } from '../../src/components/rename-station-modal';

describe('RenameStationModal', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false },
    { id: '2', name: 'Jazz Station', isQuickMix: false },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true },
  ];

  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Rename Station');
    });

    it('starts on select-station stage', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();
    });

    it('filters out QuickMix stations', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const stationItems = element.shadowRoot!.querySelectorAll('.station-item');
      const hasQuickMix = Array.from(stationItems).some(item =>
        item.textContent?.includes('QuickMix')
      );

      expect(hasQuickMix).toBe(false);
      expect(stationItems.length).toBe(2);
    });

    it('renders all selectable stations', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList!.textContent).toContain('Rock Station');
      expect(stationList!.textContent).toContain('Jazz Station');
    });
  });

  describe('station selection stage', () => {
    it('disables Next button when no station selected', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(nextButton.disabled).toBe(true);
    });

    it('enables Next button when station is selected', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(nextButton.disabled).toBe(false);
    });

    it('transitions to enter-name stage when Next is clicked', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      const nameInput = element.shadowRoot!.querySelector('.name-input');
      expect(nameInput).toBeTruthy();
    });
  });

  describe('enter-name stage', () => {
    async function setupEnterNameStage() {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      const stationItems = element.shadowRoot!.querySelectorAll('.station-item');
      const rockStation = Array.from(stationItems).find(item =>
        item.textContent?.includes('Rock Station')
      );
      const radio = rockStation!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      return element;
    }

    it('updates title with station name', async () => {
      const element = await setupEnterNameStage();

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Rename: Rock Station');
    });

    it('pre-fills name input with current station name', async () => {
      const element = await setupEnterNameStage();

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      expect(nameInput.value).toBe('Rock Station');
    });

    it('shows current station name in label', async () => {
      const element = await setupEnterNameStage();

      const currentStation = element.shadowRoot!.querySelector('.current-station');
      expect(currentStation!.textContent).toContain('Rock Station');
    });

    it('disables Rename button when name unchanged', async () => {
      const element = await setupEnterNameStage();

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(renameButton.disabled).toBe(true);
    });

    it('disables Rename button when name is empty', async () => {
      const element = await setupEnterNameStage();

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = '';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(renameButton.disabled).toBe(true);
    });

    it('enables Rename button when name is changed', async () => {
      const element = await setupEnterNameStage();

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = 'New Rock Station';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(renameButton.disabled).toBe(false);
    });

    it('dispatches rename-station event with correct details', async () => {
      const element = await setupEnterNameStage();

      let eventDetail: any;
      element.addEventListener('rename-station', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = 'New Rock Station';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      renameButton.click();

      expect(eventDetail.stationId).toBe('1');
      expect(eventDetail.newName).toBe('New Rock Station');
    });

    it('trims whitespace from new name', async () => {
      const element = await setupEnterNameStage();

      let eventDetail: any;
      element.addEventListener('rename-station', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = '  New Rock Station  ';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      renameButton.click();

      expect(eventDetail.newName).toBe('New Rock Station');
    });

    it('dispatches rename-station event on Enter key', async () => {
      const element = await setupEnterNameStage();

      let eventFired = false;
      element.addEventListener('rename-station', () => {
        eventFired = true;
      });

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = 'New Rock Station';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const enterEvent = new KeyboardEvent('keydown', { key: 'Enter' });
      nameInput.dispatchEvent(enterEvent);

      expect(eventFired).toBe(true);
    });

    it('does not dispatch on Enter if name unchanged', async () => {
      const element = await setupEnterNameStage();

      let eventFired = false;
      element.addEventListener('rename-station', () => {
        eventFired = true;
      });

      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      const enterEvent = new KeyboardEvent('keydown', { key: 'Enter' });
      nameInput.dispatchEvent(enterEvent);

      expect(eventFired).toBe(false);
    });

    it('has Back button that returns to station selection', async () => {
      const element = await setupEnterNameStage();

      const backButton = element.shadowRoot!.querySelector('.back-button') as HTMLButtonElement;
      expect(backButton).toBeTruthy();
      
      backButton.click();
      await element.updateComplete;

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Rename Station');
    });

    it('renders info note about Pandora restrictions', async () => {
      const element = await setupEnterNameStage();

      const infoNote = element.shadowRoot!.querySelector('.info-note');
      expect(infoNote).toBeTruthy();
      expect(infoNote!.textContent).toContain('Pandora may not allow');
    });
  });

  describe('cancel handling', () => {
    it('resets to initial state on cancel', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      // Select station and go to enter-name stage
      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      // Enter a new name
      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      nameInput.value = 'New Name';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel:not(.back-button)') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Rename Station');

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();
    });
  });

  describe('state reset on reopen', () => {
    it('resets to initial state when reopening after successful action', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      // Select station and go to enter-name stage
      const stationItems = element.shadowRoot!.querySelectorAll('.station-item');
      const rockStation = Array.from(stationItems).find(item =>
        item.textContent?.includes('Rock Station')
      );
      const radio = rockStation!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      // Verify we're on the enter-name stage
      const nameInput = element.shadowRoot!.querySelector('.name-input') as HTMLInputElement;
      expect(nameInput).toBeTruthy();

      // Enter a new name and click Rename (simulate successful action)
      nameInput.value = 'New Rock Station';
      nameInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const renameButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      renameButton.click();
      await element.updateComplete;

      // Close modal (simulating parent handling the event)
      element.open = false;
      await element.updateComplete;

      // Reopen modal
      element.open = true;
      await element.updateComplete;
      // Wait for state reset to complete (onCancel sets title which schedules another update)
      await element.updateComplete;

      // Should be back on the first stage (station selection)
      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Rename Station');

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();

      // Name input should not be visible (we're on step 1)
      const nameInputAfterReopen = element.shadowRoot!.querySelector('.name-input');
      expect(nameInputAfterReopen).toBeFalsy();
    });

    it('resets selected station when reopening', async () => {
      const element: RenameStationModal = await fixture(html`
        <rename-station-modal open .stations=${mockStations}></rename-station-modal>
      `);

      // Select a station
      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      // Next button should be enabled
      let nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(nextButton.disabled).toBe(false);

      // Close and reopen
      element.open = false;
      await element.updateComplete;
      element.open = true;
      await element.updateComplete;
      // Wait for state reset to complete
      await element.updateComplete;

      // Next button should be disabled again (no station selected)
      nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(nextButton.disabled).toBe(true);
    });
  });
});

