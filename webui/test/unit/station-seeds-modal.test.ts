import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/station-seeds-modal';
import type { StationSeedsModal } from '../../src/components/station-seeds-modal';

describe('StationSeedsModal', () => {
  const mockStationInfo = {
    artistSeeds: [
      { seedId: 'a1', name: 'The Beatles' },
      { seedId: 'a2', name: 'Pink Floyd' },
    ],
    songSeeds: [
      { seedId: 's1', title: 'Imagine', artist: 'John Lennon' },
      { seedId: 's2', title: 'Bohemian Rhapsody', artist: 'Queen' },
    ],
    stationSeeds: [
      { seedId: 'st1', name: 'Classic Rock Radio' },
    ],
    feedback: [
      { feedbackId: 'f1', title: 'Yesterday', artist: 'The Beatles', rating: 1 },
      { feedbackId: 'f2', title: 'Another Brick', artist: 'Pink Floyd', rating: 0 },
    ],
  };

  describe('rendering', () => {
    it('renders with default title', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal open></station-seeds-modal>
      `);

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Manage Seeds & Feedback');
    });

    it('updates title with station name', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          currentStationName="Rock Station"
        ></station-seeds-modal>
      `);

      await element.updateComplete;

      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title!.textContent).toBe('Seeds: Rock Station');
    });

    it('shows loading state when infoLoading is true', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal open infoLoading></station-seeds-modal>
      `);

      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toContain('Loading station info...');
    });

    it('shows empty state when no seeds or feedback', async () => {
      const emptyInfo = {
        artistSeeds: [],
        songSeeds: [],
        stationSeeds: [],
        feedback: [],
      };

      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${emptyInfo}
        ></station-seeds-modal>
      `);

      const noItems = element.shadowRoot!.querySelector('.no-items');
      expect(noItems).toBeTruthy();
      expect(noItems!.textContent).toContain('No seeds or feedback available');
    });

    it('renders artist seeds section', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const sectionHeader = element.shadowRoot!.querySelector('.section-header');
      expect(sectionHeader!.textContent).toContain('Artist Seeds');
      expect(sectionHeader!.textContent).toContain('(2)');
    });

    it('renders song seeds with title and artist', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const seedItems = element.shadowRoot!.querySelectorAll('.seed-item');
      const songSeedItem = Array.from(seedItems).find(item =>
        item.textContent?.includes('Imagine')
      );

      expect(songSeedItem).toBeTruthy();
      expect(songSeedItem!.textContent).toContain('Imagine');
      expect(songSeedItem!.textContent).toContain('John Lennon');
    });

    it('renders station seeds', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const content = element.shadowRoot!.querySelector('.modal-body');
      expect(content!.textContent).toContain('Station Seeds');
      expect(content!.textContent).toContain('Classic Rock Radio');
    });

    it('renders feedback with loved icon', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const feedbackItems = element.shadowRoot!.querySelectorAll('.feedback-item');
      const lovedItem = Array.from(feedbackItems).find(item =>
        item.textContent?.includes('Yesterday')
      );

      expect(lovedItem).toBeTruthy();
      const icon = lovedItem!.querySelector('.feedback-icon.loved');
      expect(icon).toBeTruthy();
      expect(icon!.textContent).toContain('thumb_up');
    });

    it('renders feedback with banned icon', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const feedbackItems = element.shadowRoot!.querySelectorAll('.feedback-item');
      const bannedItem = Array.from(feedbackItems).find(item =>
        item.textContent?.includes('Another Brick')
      );

      expect(bannedItem).toBeTruthy();
      const icon = bannedItem!.querySelector('.feedback-icon.banned');
      expect(icon).toBeTruthy();
      expect(icon!.textContent).toContain('thumb_down');
    });

    it('renders delete buttons for all seeds', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const deleteButtons = element.shadowRoot!.querySelectorAll('.delete-button');
      // 2 artists + 2 songs + 1 station + 2 feedback = 7 delete buttons
      expect(deleteButtons.length).toBeGreaterThanOrEqual(7);
    });
  });

  describe('section expansion', () => {
    it('starts with all sections expanded', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const chevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(chevrons.length).toBe(4); // All 4 sections expanded
    });

    it('collapses section when header is clicked', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const sectionHeaders = element.shadowRoot!.querySelectorAll('.section-header');
      const artistSeedsHeader = Array.from(sectionHeaders).find(header =>
        header.textContent?.includes('Artist Seeds')
      ) as HTMLElement;

      const chevron = artistSeedsHeader.querySelector('.chevron');
      expect(chevron!.classList.contains('expanded')).toBe(true);

      artistSeedsHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(false);
    });

    it('expands collapsed section when header is clicked again', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const sectionHeaders = element.shadowRoot!.querySelectorAll('.section-header');
      const artistSeedsHeader = Array.from(sectionHeaders).find(header =>
        header.textContent?.includes('Artist Seeds')
      ) as HTMLElement;

      // Collapse
      artistSeedsHeader.click();
      await element.updateComplete;

      const chevron = artistSeedsHeader.querySelector('.chevron');
      expect(chevron!.classList.contains('expanded')).toBe(false);

      // Expand
      artistSeedsHeader.click();
      await element.updateComplete;

      expect(chevron!.classList.contains('expanded')).toBe(true);
    });

    it('hides section content when collapsed', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      const sectionHeaders = element.shadowRoot!.querySelectorAll('.section-header');
      const artistSeedsHeader = Array.from(sectionHeaders).find(header =>
        header.textContent?.includes('Artist Seeds')
      ) as HTMLElement;

      // First verify it's expanded
      const chevronBefore = artistSeedsHeader.querySelector('.chevron');
      expect(chevronBefore!.classList.contains('expanded')).toBe(true);

      // Collapse it
      artistSeedsHeader.click();
      await element.updateComplete;

      // Check that chevron is no longer expanded
      const chevronAfter = artistSeedsHeader.querySelector('.chevron');
      expect(chevronAfter!.classList.contains('expanded')).toBe(false);

      // The content should not be rendered (no section-content with Beatles)
      const allSections = element.shadowRoot!.querySelectorAll('.section-content');
      const hasBeatlesInAny = Array.from(allSections).some(section => {
        const parent = section.parentElement;
        const header = parent?.querySelector('.section-header');
        return header?.textContent?.includes('Artist Seeds') && section.textContent?.includes('The Beatles');
      });
      expect(hasBeatlesInAny).toBe(false);
    });
  });

  describe('delete actions', () => {
    it('dispatches delete-seed event for artist seed', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          currentStationId="station1"
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('delete-seed', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const seedItems = element.shadowRoot!.querySelectorAll('.seed-item');
      const beatlesSeed = Array.from(seedItems).find(item =>
        item.textContent?.includes('The Beatles')
      );
      const deleteButton = beatlesSeed!.querySelector('.delete-button') as HTMLButtonElement;

      deleteButton.click();

      expect(eventFired).toBe(true);
      expect(eventDetail.seedId).toBe('a1');
      expect(eventDetail.seedType).toBe('artist');
      expect(eventDetail.stationId).toBe('station1');
    });

    it('dispatches delete-seed event for song seed', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          currentStationId="station1"
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      let eventDetail: any;
      element.addEventListener('delete-seed', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const seedItems = element.shadowRoot!.querySelectorAll('.seed-item');
      const imagineSeed = Array.from(seedItems).find(item =>
        item.textContent?.includes('Imagine')
      );
      const deleteButton = imagineSeed!.querySelector('.delete-button') as HTMLButtonElement;

      deleteButton.click();

      expect(eventDetail.seedId).toBe('s1');
      expect(eventDetail.seedType).toBe('song');
    });

    it('dispatches delete-seed event for station seed', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          currentStationId="station1"
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      let eventDetail: any;
      element.addEventListener('delete-seed', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const seedItems = element.shadowRoot!.querySelectorAll('.seed-item');
      const stationSeed = Array.from(seedItems).find(item =>
        item.textContent?.includes('Classic Rock Radio')
      );
      const deleteButton = stationSeed!.querySelector('.delete-button') as HTMLButtonElement;

      deleteButton.click();

      expect(eventDetail.seedId).toBe('st1');
      expect(eventDetail.seedType).toBe('station');
    });

    it('dispatches delete-feedback event for feedback', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          currentStationId="station1"
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      let eventFired = false;
      let eventDetail: any;
      element.addEventListener('delete-feedback', ((e: CustomEvent) => {
        eventFired = true;
        eventDetail = e.detail;
      }) as EventListener);

      const feedbackItems = element.shadowRoot!.querySelectorAll('.feedback-item');
      const yesterdayFeedback = Array.from(feedbackItems).find(item =>
        item.textContent?.includes('Yesterday')
      );
      const deleteButton = yesterdayFeedback!.querySelector('.delete-button') as HTMLButtonElement;

      deleteButton.click();

      expect(eventFired).toBe(true);
      expect(eventDetail.feedbackId).toBe('f1');
      expect(eventDetail.stationId).toBe('station1');
    });
  });

  describe('cancel handling', () => {
    it('resets expanded sections on cancel', async () => {
      const element: StationSeedsModal = await fixture(html`
        <station-seeds-modal 
          open
          .stationInfo=${mockStationInfo}
        ></station-seeds-modal>
      `);

      // Collapse all sections
      const sectionHeaders = element.shadowRoot!.querySelectorAll('.section-header');
      sectionHeaders.forEach((header: Element) => (header as HTMLElement).click());
      await element.updateComplete;

      let expandedChevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(expandedChevrons.length).toBe(0);

      // Cancel and reopen
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();

      element.open = true;
      await element.updateComplete;

      expandedChevrons = element.shadowRoot!.querySelectorAll('.chevron.expanded');
      expect(expandedChevrons.length).toBe(4); // All sections expanded again
    });
  });
});

