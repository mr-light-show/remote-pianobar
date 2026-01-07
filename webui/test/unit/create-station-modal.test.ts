import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/create-station-modal';
import type { CreateStationModal } from '../../src/components/create-station-modal';

describe('CreateStationModal', () => {
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
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Create Station');
    });

    it('starts in select mode', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const selectOptions = element.shadowRoot!.querySelector('.select-options');
      expect(selectOptions).toBeTruthy();
    });

    it('renders search input in select mode', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const searchInput = element.shadowRoot!.querySelector('.search-input');
      expect(searchInput).toBeTruthy();
    });

    it('renders current artist button', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentArtistName="The Beatles"
          currentTrackToken="token123"
        ></create-station-modal>
      `);

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const artistButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Artist')
      );

      expect(artistButton).toBeTruthy();
      expect(artistButton!.textContent).toContain('The Beatles');
    });

    it('renders current song button', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentSongName="Imagine"
          currentTrackToken="token123"
        ></create-station-modal>
      `);

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const songButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Song')
      );

      expect(songButton).toBeTruthy();
      expect(songButton!.textContent).toContain('Imagine');
    });

    it('disables artist button when no track token', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentArtistName="The Beatles"
        ></create-station-modal>
      `);

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const artistButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Artist')
      ) as HTMLButtonElement;

      expect(artistButton.disabled).toBe(true);
    });

    it('disables song button when no track token', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentSongName="Imagine"
        ></create-station-modal>
      `);

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const songButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Song')
      ) as HTMLButtonElement;

      expect(songButton.disabled).toBe(true);
    });
  });

  describe('select mode interactions', () => {
    it('dispatches select-artist event when artist button clicked', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentArtistName="The Beatles"
          currentTrackToken="token123"
        ></create-station-modal>
      `);

      let eventFired = false;
      element.addEventListener('select-artist', () => {
        eventFired = true;
      });

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const artistButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Artist')
      ) as HTMLButtonElement;

      artistButton.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches select-song event when song button clicked', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal 
          open
          currentSongName="Imagine"
          currentTrackToken="token123"
        ></create-station-modal>
      `);

      let eventFired = false;
      element.addEventListener('select-song', () => {
        eventFired = true;
      });

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const songButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('current Song')
      ) as HTMLButtonElement;

      songButton.click();

      expect(eventFired).toBe(true);
    });

    it('enables search button when input has text', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

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
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      let eventDetail: any;
      element.addEventListener('search', ((e: CustomEvent) => {
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

      expect(eventDetail.query).toBe('Beatles');
    });

    it('transitions to search-results mode when searching', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      searchButton.click();
      await element.updateComplete;

      // Should no longer show select-options
      const selectOptions = element.shadowRoot!.querySelector('.select-options');
      expect(selectOptions).toBeFalsy();
    });

    it('dispatches search event on Enter key', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

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
  });

  describe('search-results mode', () => {
    async function setupSearchResultsMode() {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      searchButton.click();
      await element.updateComplete;

      element.searchResults = mockSearchResults;
      await element.updateComplete;

      return element;
    }

    it('shows loading state during search', async () => {
      const element = await setupSearchResultsMode();
      element.loading = true;
      await element.updateComplete;

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Searching...');
    });

    it('shows no results message when search returns empty', async () => {
      const element = await setupSearchResultsMode();
      element.searchResults = { categories: [] };
      await element.updateComplete;

      const noResults = element.shadowRoot!.querySelector('.no-results');
      expect(noResults).toBeTruthy();
      expect(noResults!.textContent).toContain('No results found');
    });

    it('renders search results with categories', async () => {
      const element = await setupSearchResultsMode();

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      expect(categoryHeaders.length).toBe(2);
    });

    it('expands and collapses categories', async () => {
      const element = await setupSearchResultsMode();

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      const chevron = categoryHeader.querySelector('.chevron');
      
      expect(chevron!.classList.contains('expanded')).toBe(false);

      categoryHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(true);
    });

    it('enables Create Station button when result is selected', async () => {
      const element = await setupSearchResultsMode();

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(false);
    });

    it('dispatches create event with musicId', async () => {
      const element = await setupSearchResultsMode();

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('create', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      // Call handleResultSelect directly to set the internal state
      element.handleResultSelect('a1');
      await element.updateComplete;

      // Call handleCreate directly since button click may not trigger @click handler in tests
      element.handleCreate();

      expect(eventFired).toBe(true);
      expect(eventDetail).toBeTruthy();
      expect(eventDetail.musicId).toBe('a1');
    });
  });

  describe('browse-genres mode', () => {
    const mockGenreCategories = [
      {
        name: 'Rock',
        genres: [
          { name: 'Classic Rock', musicId: 'g1' },
          { name: 'Alternative Rock', musicId: 'g2' },
        ],
      },
      {
        name: 'Pop',
        genres: [
          { name: 'Top 40', musicId: 'g3' },
          { name: 'Dance Pop', musicId: 'g4' },
        ],
      },
    ];

    async function setupBrowseGenresMode() {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      // Click the Genre button to enter browse-genres mode
      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const genreButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('Genre')
      ) as HTMLButtonElement;
      genreButton.click();
      await element.updateComplete;

      return element;
    }

    it('renders genre option button in select mode', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const genreButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('Genre')
      );

      expect(genreButton).toBeTruthy();
      expect(genreButton!.textContent).toContain('Select a genre');
    });

    it('dispatches browse-genres event when genre button clicked', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      let eventFired = false;
      element.addEventListener('browse-genres', () => {
        eventFired = true;
      });

      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const genreButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('Genre')
      ) as HTMLButtonElement;
      genreButton.click();

      expect(eventFired).toBe(true);
    });

    it('transitions to browse-genres mode when genre button clicked', async () => {
      const element = await setupBrowseGenresMode();

      // Should no longer show select-options
      const selectOptions = element.shadowRoot!.querySelector('.select-options');
      expect(selectOptions).toBeFalsy();
    });

    it('shows loading state when genreLoading is true', async () => {
      const element = await setupBrowseGenresMode();
      element.genreLoading = true;
      await element.updateComplete;

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Loading genres...');
    });

    it('shows no genres message when genreCategories is empty', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = [];
      await element.updateComplete;

      const noResults = element.shadowRoot!.querySelector('.no-results');
      expect(noResults).toBeTruthy();
      expect(noResults!.textContent).toContain('No genres available');
    });

    it('renders genre categories', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      expect(categoryHeaders.length).toBe(2);
    });

    it('expands and collapses genre categories', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      const chevron = categoryHeader.querySelector('.chevron');
      
      expect(chevron!.classList.contains('expanded')).toBe(false);

      categoryHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(true);
    });

    it('shows genres when category is expanded', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      // Expand first category
      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      const genreItems = element.shadowRoot!.querySelectorAll('.list-item');
      expect(genreItems.length).toBe(2); // Classic Rock and Alternative Rock
    });

    it('enables Create Station button when genre is selected', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      // Expand first category
      const categoryHeader = element.shadowRoot!.querySelector('.category-header') as HTMLElement;
      categoryHeader.click();
      await element.updateComplete;

      // Select a genre
      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(false);
    });

    it('dispatches genre-create event with musicId', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('genre-create', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      // Select a genre directly
      element.handleGenreSelect('g1');
      await element.updateComplete;

      // Call handleGenreCreate
      element.handleGenreCreate();

      expect(eventFired).toBe(true);
      expect(eventDetail).toBeTruthy();
      expect(eventDetail.musicId).toBe('g1');
    });

    it('returns to select mode when Back button clicked', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      const backButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      backButton.click();
      await element.updateComplete;

      const selectOptions = element.shadowRoot!.querySelector('.select-options');
      expect(selectOptions).toBeTruthy();
    });

    it('clears selection when returning to select mode', async () => {
      const element = await setupBrowseGenresMode();
      element.genreCategories = mockGenreCategories;
      await element.updateComplete;

      // Select a genre
      element.handleGenreSelect('g1');
      await element.updateComplete;

      // Go back
      const backButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      backButton.click();
      await element.updateComplete;

      // Re-enter browse-genres mode
      const optionButtons = element.shadowRoot!.querySelectorAll('.option-button');
      const genreButton = Array.from(optionButtons).find(btn =>
        btn.textContent?.includes('Genre')
      ) as HTMLButtonElement;
      genreButton.click();
      await element.updateComplete;

      // Create button should be disabled (no selection)
      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(true);
    });
  });

  describe('cancel handling', () => {
    it('resets to select mode on cancel', async () => {
      const element: CreateStationModal = await fixture(html`
        <create-station-modal open></create-station-modal>
      `);

      // Go to search-results mode
      const searchInput = element.shadowRoot!.querySelector('.search-input') as HTMLInputElement;
      searchInput.value = 'Beatles';
      searchInput.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const searchButton = Array.from(element.shadowRoot!.querySelectorAll('.button-confirm')).find(btn =>
        btn.textContent?.trim() === 'Search'
      ) as HTMLButtonElement;
      searchButton.click();
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const selectOptions = element.shadowRoot!.querySelector('.select-options');
      expect(selectOptions).toBeTruthy();
    });
  });
});

