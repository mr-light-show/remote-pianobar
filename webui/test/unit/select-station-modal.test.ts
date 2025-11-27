import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/select-station-modal';
import type { SelectStationModal } from '../../src/components/select-station-modal';

describe('SelectStationModal', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false },
    { id: '2', name: 'Jazz Station', isQuickMix: false },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true },
  ];

  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Select Station');
    });

    it('renders all stations by default', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      expect(listItems.length).toBe(3);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Rock Station');
      expect(content!.textContent).toContain('Jazz Station');
      expect(content!.textContent).toContain('QuickMix');
    });

    it('excludes QuickMix when excludeQuickMix is true', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal 
          open 
          .stations=${mockStations}
          excludeQuickMix
        ></select-station-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      expect(listItems.length).toBe(2);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Rock Station');
      expect(content!.textContent).toContain('Jazz Station');
      expect(content!.textContent).not.toContain('QuickMix');
    });

    it('shows message when no stations available', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${[]}></select-station-modal>
      `);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('No stations available');
    });

    it('renders radio buttons for each station', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      expect(radios.length).toBe(3);
    });

    it('renders confirm button with default text', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm');
      expect(confirmButton!.textContent?.trim()).toBe('Select');
    });

    it('renders confirm button with custom text', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal 
          open 
          .stations=${mockStations}
          confirmText="Delete"
        ></select-station-modal>
      `);

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm');
      expect(confirmButton!.textContent?.trim()).toBe('Delete');
    });

    it('applies danger class when confirmDanger is true', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal 
          open 
          .stations=${mockStations}
          confirmDanger
        ></select-station-modal>
      `);

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm');
      expect(confirmButton!.classList.contains('button-danger')).toBe(true);
    });
  });

  describe('station selection', () => {
    it('starts with no station selected', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(0);
    });

    it('selects station when radio is clicked', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      expect(radio.checked).toBe(true);
    });

    it('selects station when list item is clicked', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const listItem = element.shadowRoot!.querySelector('.list-item') as HTMLElement;
      listItem.click();
      await element.updateComplete;

      const radio = listItem.querySelector('input[type="radio"]') as HTMLInputElement;
      expect(radio.checked).toBe(true);
    });

    it('adds selected class to selected list item', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const listItem = element.shadowRoot!.querySelector('.list-item') as HTMLElement;
      listItem.click();
      await element.updateComplete;

      expect(listItem.classList.contains('selected')).toBe(true);
    });

    it('allows only one station to be selected', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      (listItems[0] as HTMLElement).click();
      await element.updateComplete;
      (listItems[1] as HTMLElement).click();
      await element.updateComplete;

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(1);
      expect((radios[1] as HTMLInputElement).checked).toBe(true);
    });
  });

  describe('confirm functionality', () => {
    it('disables confirm button when no station selected', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(confirmButton.disabled).toBe(true);
    });

    it('enables confirm button when station is selected', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(confirmButton.disabled).toBe(false);
    });

    it('dispatches station-select event with stationId', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      let eventDetail: any;
      element.addEventListener('station-select', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      confirmButton.click();

      expect(eventDetail.stationId).toBe('1');
    });

    it('clears selection after confirming', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      confirmButton.click();
      await element.updateComplete;

      // Check that confirm button is disabled again
      expect(confirmButton.disabled).toBe(true);
    });
  });

  describe('cancel handling', () => {
    it('clears selection on cancel', async () => {
      const element: SelectStationModal = await fixture(html`
        <select-station-modal open .stations=${mockStations}></select-station-modal>
      `);

      // Select a station
      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(confirmButton.disabled).toBe(true);
    });
  });
});

