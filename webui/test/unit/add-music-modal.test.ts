import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/add-music-modal';
import type { AddMusicModal } from '../../src/components/add-music-modal';

describe('AddMusicModal', () => {
  const mockStations = [
    { id: '1', name: 'Rock Station', isQuickMix: false },
    { id: '2', name: 'Jazz Station', isQuickMix: false },
    { id: 'quickmix', name: 'QuickMix', isQuickMix: true },
  ];

  const mockSearchResults = {
    categories: [
      {
        name: 'Artists',
        results: [
          { name: 'The Beatles', musicId: 'a1' },
          { name: 'Pink Floyd', musicId: 'a2' },
        ],
      },
      {
        name: 'Songs',
        results: [
          { title: 'Imagine', artist: 'John Lennon', musicId: 's1' },
          { title: 'Yesterday', artist: 'The Beatles', musicId: 's2' },
        ],
      },
    ],
  };

  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Add Music to Station');
    });

    it('starts on select-station stage', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();
    });

    it('filters out QuickMix stations in selection', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const stationItems = element.shadowRoot!.querySelectorAll('.station-item');
      const hasQuickMix = Array.from(stationItems).some(item =>
        item.textContent?.includes('QuickMix')
      );

      expect(hasQuickMix).toBe(false);
      expect(stationItems.length).toBe(2); // Only non-QuickMix stations
    });

    it('renders all selectable stations', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList!.textContent).toContain('Rock Station');
      expect(stationList!.textContent).toContain('Jazz Station');
    });
  });

  describe('station selection stage', () => {
    it('enables Next button when station is selected', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(nextButton.disabled).toBe(true);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      expect(nextButton.disabled).toBe(false);
    });

    it('transitions to search stage when Next is clicked', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      const searchInput = element.shadowRoot!.querySelector('.search-input');
      expect(searchInput).toBeTruthy();
    });

    it('updates title when transitioning to search stage', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
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

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Add Music to Rock Station');
    });
  });

  describe('search stage', () => {
    async function setupSearchStage() {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      return element;
    }

    it('renders search input', async () => {
      const element = await setupSearchStage();

      const searchInput = element.shadowRoot!.querySelector('.search-input');
      expect(searchInput).toBeTruthy();
    });

    it('shows prompt message before search', async () => {
      const element = await setupSearchStage();

      const noResults = element.shadowRoot!.querySelector('.no-results');
      expect(noResults!.textContent).toContain('Search for an artist or song');
    });

    it('disables search button when input is empty', async () => {
      const element = await setupSearchStage();

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      expect(searchButton.disabled).toBe(true);
    });

    it('enables search button when input has text', async () => {
      const element = await setupSearchStage();

      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      expect(searchButton.disabled).toBe(false);
    });

    it('dispatches search event when Search button clicked', async () => {
      const element = await setupSearchStage();

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('search', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      searchButton.click();

      expect(eventFired).toBe(true);
      expect(eventDetail.query).toBe('Beatles');
    });

    it('dispatches search event on Enter key', async () => {
      const element = await setupSearchStage();

      let eventFired = false;
      element.addEventListener('search', () => {
        eventFired = true;
      });

      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const enterEvent = new KeyboardEvent('keydown', { key: 'Enter' });
      searchInput.dispatchEvent(enterEvent);

      expect(eventFired).toBe(true);
    });

    it('shows loading state during search', async () => {
      const element = await setupSearchStage();
      element.loading = true;
      await element.updateComplete;

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Searching...');
    });

    it('shows no results message when search returns empty', async () => {
      const element = await setupSearchStage();
      element.searchResults = { categories: [] };
      (element as any).searchPerformed = true;
      await element.updateComplete;

      const noResults = element.shadowRoot!.querySelector('.no-results');
      expect(noResults!.textContent).toContain('No results found');
    });

    it('renders search results with categories', async () => {
      const element = await setupSearchStage();
      element.searchResults = mockSearchResults;
      await element.updateComplete;

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      expect(categoryHeaders.length).toBe(2);
      
      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Artists');
      expect(content!.textContent).toContain('Songs');
    });

    it('expands and collapses categories', async () => {
      const element = await setupSearchStage();
      element.searchResults = mockSearchResults;
      await element.updateComplete;

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      const chevron = categoryHeader.querySelector('.chevron');
      
      expect(chevron!.classList.contains('expanded')).toBe(false);

      categoryHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(true);
    });

    it('shows results when category is expanded', async () => {
      const element = await setupSearchStage();
      element.searchResults = mockSearchResults;
      await element.updateComplete;

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      const resultsList = element.shadowRoot!.querySelector('.results-list');
      expect(resultsList).toBeTruthy();
      expect(resultsList!.textContent).toContain('The Beatles');
      expect(resultsList!.textContent).toContain('Pink Floyd');
    });

    it('enables Add Music button when result is selected', async () => {
      const element = await setupSearchStage();
      element.searchResults = mockSearchResults;
      await element.updateComplete;

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      const addButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Add Music'
      ) as HTMLButtonElement;
      expect(addButton.disabled).toBe(true);

      const radio = element.shadowRoot!.querySelector('input[type="radio"][name="search-result-select"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      expect(addButton.disabled).toBe(false);
    });

    it('dispatches add-music event with musicId and stationId', async () => {
      const element = await setupSearchStage();
      element.searchResults = mockSearchResults;
      await element.updateComplete;

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"][name="search-result-select"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('add-music', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const addButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Add Music'
      ) as HTMLButtonElement;
      addButton.click();

      expect(eventDetail.musicId).toBe('a1');
      expect(eventDetail.stationId).toBe('1');
    });

    it('has Back button that returns to station selection', async () => {
      const element = await setupSearchStage();

      const backButton = element.shadowRoot!.querySelector('.back-button') as HTMLButtonElement;
      expect(backButton).toBeTruthy();
      
      backButton.click();
      await element.updateComplete;

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();
    });
  });

  describe('cancel handling', () => {
    it('resets to initial state on cancel', async () => {
      const element: AddMusicModal = await fixture(html`
        <add-music-modal open .stations=${mockStations}></add-music-modal>
      `);

      // Select station and go to search stage
      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const nextButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      nextButton.click();
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel:not(.back-button)') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Add Music to Station');

      const stationList = element.shadowRoot!.querySelector('.station-list');
      expect(stationList).toBeTruthy();
    });
  });
});

