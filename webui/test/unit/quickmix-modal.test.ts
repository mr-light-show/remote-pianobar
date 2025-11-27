import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/quickmix-modal';
import type { QuickMixModal } from '../../src/components/quickmix-modal';

describe('QuickMixModal', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false, isQuickMixed: true },
    { id: '2', name: 'Jazz Station', isQuickMix: false, isQuickMixed: false },
    { id: '3', name: 'Classical Station', isQuickMix: false, isQuickMixed: true },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true, isQuickMixed: false },
  ];

  describe('rendering', () => {
    it('renders with title', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Select QuickMix Stations');
    });

    it('filters out QuickMix station itself', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const hasQuickMix = Array.from(listItems).some(item =>
        item.textContent?.includes('QuickMix')
      );

      expect(hasQuickMix).toBe(false);
      expect(listItems.length).toBe(3);
    });

    it('renders all selectable stations with checkboxes', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const checkboxes = element.shadowRoot!.querySelectorAll('input[type="checkbox"]');
      expect(checkboxes.length).toBe(3);

      const listContainer = element.shadowRoot!.querySelector('.list-container');
      expect(listContainer!.textContent).toContain('Rock Station');
      expect(listContainer!.textContent).toContain('Jazz Station');
      expect(listContainer!.textContent).toContain('Classical Station');
    });

    it('pre-checks stations based on isQuickMixed', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      
      const rockStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Rock Station')
      );
      const rockCheckbox = rockStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      expect(rockCheckbox.checked).toBe(true);

      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      expect(jazzCheckbox.checked).toBe(false);

      const classicalStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Classical Station')
      );
      const classicalCheckbox = classicalStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      expect(classicalCheckbox.checked).toBe(true);
    });
  });

  describe('checkbox interactions', () => {
    it('updates selection when checkbox is checked', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;

      expect(jazzCheckbox.checked).toBe(false);

      jazzCheckbox.click();
      await element.updateComplete;

      expect(jazzCheckbox.checked).toBe(true);
    });

    it('updates selection when checkbox is unchecked', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const rockStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Rock Station')
      );
      const rockCheckbox = rockStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;

      expect(rockCheckbox.checked).toBe(true);

      rockCheckbox.click();
      await element.updateComplete;

      expect(rockCheckbox.checked).toBe(false);
    });

    it('allows multiple stations to be selected', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      
      // Check Jazz Station
      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      jazzCheckbox.click();
      await element.updateComplete;

      // Verify all three are now checked
      const checkboxes = element.shadowRoot!.querySelectorAll('input[type="checkbox"]');
      const checkedCount = Array.from(checkboxes).filter((cb: Element) => 
        (cb as HTMLInputElement).checked
      ).length;

      expect(checkedCount).toBe(3);
    });
  });

  describe('save functionality', () => {
    it('dispatches save event with selected stationIds', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      let eventDetail: any;
      element.addEventListener('save', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const saveButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      saveButton.click();

      expect(eventDetail).toBeTruthy();
      expect(eventDetail.stationIds).toBeInstanceOf(Array);
      expect(eventDetail.stationIds).toContain('1'); // Rock Station
      expect(eventDetail.stationIds).toContain('3'); // Classical Station
      expect(eventDetail.stationIds).not.toContain('2'); // Jazz Station (not checked)
      expect(eventDetail.stationIds).not.toContain('quickmix'); // QuickMix itself
    });

    it('dispatches save event with updated selections', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      // Uncheck Rock Station
      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const rockStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Rock Station')
      );
      const rockCheckbox = rockStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      rockCheckbox.click();
      await element.updateComplete;

      // Check Jazz Station
      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      jazzCheckbox.click();
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('save', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const saveButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      saveButton.click();

      expect(eventDetail.stationIds).not.toContain('1'); // Rock Station (unchecked)
      expect(eventDetail.stationIds).toContain('2'); // Jazz Station (checked)
      expect(eventDetail.stationIds).toContain('3'); // Classical Station (still checked)
    });

    it('dispatches save event with empty array if none selected', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      // Uncheck all
      const checkboxes = element.shadowRoot!.querySelectorAll('input[type="checkbox"]');
      checkboxes.forEach((checkbox: Element) => {
        if ((checkbox as HTMLInputElement).checked) {
          (checkbox as HTMLInputElement).click();
        }
      });
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('save', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const saveButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      saveButton.click();

      expect(eventDetail.stationIds).toEqual([]);
    });
  });

  describe('state management', () => {
    it('reinitializes selection when stations prop changes', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      // Modify selection
      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      jazzCheckbox.click();
      await element.updateComplete;

      // Update stations prop
      const updatedStations = [
        { id: '1', name: 'Rock Station', isQuickMix: false, isQuickMixed: false },
        { id: '2', name: 'Jazz Station', isQuickMix: false, isQuickMixed: true },
      ];
      element.stations = updatedStations;
      await element.updateComplete;

      // Check that selection was reinitialized
      const newListItems = element.shadowRoot!.querySelectorAll('.list-item');
      const newJazzItem = Array.from(newListItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const newJazzCheckbox = newJazzItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      expect(newJazzCheckbox.checked).toBe(true); // Based on updated isQuickMixed
    });

    it('reinitializes selection when modal is reopened', async () => {
      const element: QuickMixModal = await fixture(html`
        <quickmix-modal open .stations=${mockStations}></quickmix-modal>
      `);

      // Modify selection
      const listItems = element.shadowRoot!.querySelectorAll('.list-item');
      const jazzStationItem = Array.from(listItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const jazzCheckbox = jazzStationItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      jazzCheckbox.click();
      await element.updateComplete;

      // Close and reopen
      element.open = false;
      await element.updateComplete;
      element.open = true;
      await element.updateComplete;

      // Check that selection was reinitialized
      const newListItems = element.shadowRoot!.querySelectorAll('.list-item');
      const newJazzItem = Array.from(newListItems).find(item =>
        item.textContent?.includes('Jazz Station')
      );
      const newJazzCheckbox = newJazzItem!.querySelector('input[type="checkbox"]') as HTMLInputElement;
      expect(newJazzCheckbox.checked).toBe(false); // Reset to original isQuickMixed
    });
  });
});

