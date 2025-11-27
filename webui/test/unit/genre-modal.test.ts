import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/genre-modal';
import type { GenreModal } from '../../src/components/genre-modal';

describe('GenreModal', () => {
  const mockCategories = [
    {
      name: 'Rock',
      genres: [
        { name: 'Classic Rock', musicId: 'g1' },
        { name: 'Alternative Rock', musicId: 'g2' },
        { name: 'Hard Rock', musicId: 'g3' },
      ],
    },
    {
      name: 'Jazz',
      genres: [
        { name: 'Smooth Jazz', musicId: 'g4' },
        { name: 'Bebop', musicId: 'g5' },
      ],
    },
  ];

  describe('rendering', () => {
    it('renders with title', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Add Genre Station');
    });

    it('shows loading state when loading is true', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open loading></genre-modal>
      `);

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Fetching genres...');
    });

    it('renders all categories', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      expect(categoryHeaders.length).toBe(2);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Rock');
      expect(content!.textContent).toContain('Jazz');
    });

    it('starts with all categories collapsed', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const chevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(chevrons.length).toBe(0);
    });

    it('renders chevron icons for categories', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const chevrons = element.shadowRoot!.querySelectorAll('.chevron');
      expect(chevrons.length).toBe(2);
    });
  });

  describe('category expansion', () => {
    it('expands category when header is clicked', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      const chevron = rockHeader.querySelector('.chevron');

      expect(chevron!.classList.contains('expanded')).toBe(false);

      rockHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(true);
    });

    it('shows genre list when category is expanded', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;

      rockHeader.click();
      await element.updateComplete;

      const genreList = element.shadowRoot!.querySelector('.genre-list');
      expect(genreList).toBeTruthy();
      expect(genreList!.textContent).toContain('Classic Rock');
      expect(genreList!.textContent).toContain('Alternative Rock');
      expect(genreList!.textContent).toContain('Hard Rock');
    });

    it('collapses category when header is clicked again', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;

      // Expand
      rockHeader.click();
      await element.updateComplete;

      const chevron = rockHeader.querySelector('.chevron');
      expect(chevron!.classList.contains('expanded')).toBe(true);

      // Collapse
      rockHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(false);
    });

    it('hides genre list when category is collapsed', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;

      // Expand then collapse
      rockHeader.click();
      await element.updateComplete;
      rockHeader.click();
      await element.updateComplete;

      const genreLists = element.shadowRoot!.querySelectorAll('.genre-list');
      expect(genreLists.length).toBe(0);
    });

    it('allows multiple categories to be expanded simultaneously', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      const jazzHeader = categoryHeaders[1] as HTMLElement;

      rockHeader.click();
      await element.updateComplete;
      jazzHeader.click();
      await element.updateComplete;

      const expandedChevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(expandedChevrons.length).toBe(2);
    });
  });

  describe('genre selection', () => {
    it('renders radio buttons for genres', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      expect(radios.length).toBe(3); // Three rock genres
    });

    it('selects genre when radio is clicked', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      expect(radio.checked).toBe(true);
    });

    it('allows only one genre to be selected', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      // Expand both categories
      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      (categoryHeaders[0] as HTMLElement).click();
      await element.updateComplete;
      (categoryHeaders[1] as HTMLElement).click();
      await element.updateComplete;

      const radios = element.shadowRoot!.querySelectorAll('input[type="radio"]');
      
      (radios[0] as HTMLInputElement).click();
      await element.updateComplete;
      
      (radios[3] as HTMLInputElement).click();
      await element.updateComplete;

      const checkedRadios = Array.from(radios).filter((radio: Element) =>
        (radio as HTMLInputElement).checked
      );

      expect(checkedRadios.length).toBe(1);
      expect((radios[3] as HTMLInputElement).checked).toBe(true);
    });
  });

  describe('create station functionality', () => {
    it('disables Create Station button when no genre selected', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(true);
    });

    it('enables Create Station button when genre is selected', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(false);
    });

    it('disables Create Station button during loading', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories} loading></genre-modal>
      `);

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(true);
    });

    it('dispatches create event with musicId', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      let eventDetail: any;
      element.addEventListener('create', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      createButton.click();

      expect(eventDetail.musicId).toBe('g1');
    });

    it('clears selection after creating station', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      createButton.click();
      await element.updateComplete;

      expect(createButton.disabled).toBe(true);
    });
  });

  describe('cancel handling', () => {
    it('resets selection and collapses categories on cancel', async () => {
      const element: GenreModal = await fixture(html`
        <genre-modal open .categories=${mockCategories}></genre-modal>
      `);

      // Expand category and select genre
      const categoryHeaders = element.shadowRoot!.querySelectorAll('.category-header');
      const rockHeader = categoryHeaders[0] as HTMLElement;
      rockHeader.click();
      await element.updateComplete;

      const radio = element.shadowRoot!.querySelector('input[type="radio"]') as HTMLInputElement;
      radio.click();
      await element.updateComplete;

      // Cancel
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();

      // Reopen
      element.open = true;
      await element.updateComplete;

      const expandedChevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(expandedChevrons.length).toBe(0);

      const createButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(createButton.disabled).toBe(true);
    });
  });
});

