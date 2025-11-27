import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/song-actions-menu';
import type { SongActionsMenu } from '../../src/components/song-actions-menu';

describe('SongActionsMenu', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  describe('rendering', () => {
    it('starts with menu closed', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('renders all action buttons', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      const actionButtons = element.shadowRoot!.querySelectorAll('.action-button');
      expect(actionButtons.length).toBe(3); // Love, Ban, Tired
    });

    it('renders Love Song button', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Love Song');
      
      const loveIcon = element.shadowRoot!.querySelector('.action-button.love .material-icons');
      expect(loveIcon!.textContent).toContain('thumb_up');
    });

    it('renders Ban Song button', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Ban Song');
      
      const banButton = element.shadowRoot!.querySelector('.action-button.ban');
      expect(banButton).toBeTruthy();
      const banIcon = banButton!.querySelector('.material-icons');
      expect(banIcon!.textContent).toContain('thumb_down');
    });

    it('renders Snooze button', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Snooze');
      expect(content!.textContent).toContain('1 month');
      
      const tiredButton = element.shadowRoot!.querySelector('.action-button.tired');
      expect(tiredButton).toBeTruthy();
      const tiredIcon = tiredButton!.querySelector('.material-icons');
      expect(tiredIcon!.textContent).toContain('snooze');
    });

    it('does not show active class on Love button when rating is 0', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu rating="0"></song-actions-menu>
      `);

      const loveButton = element.shadowRoot!.querySelector('.action-button.love');
      expect(loveButton!.classList.contains('active')).toBe(false);
    });

    it('shows active class on Love button when rating is 1', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu rating="1"></song-actions-menu>
      `);

      const loveButton = element.shadowRoot!.querySelector('.action-button.love');
      expect(loveButton!.classList.contains('active')).toBe(true);
    });
  });

  describe('menu toggle', () => {
    it('uses setTimeout(0) pattern for toggle', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      
      // Menu should not open immediately
      let menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);

      // Advance timers
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Menu should now be open
      menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(false);
    });

    it('toggles menu open and closed', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      // Open
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(false);

      // Close
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });
  });

  describe('outside click dismissal', () => {
    it('adds document click listener on connect', async () => {
      const addEventListenerSpy = vi.spyOn(document, 'addEventListener');

      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      expect(addEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      addEventListenerSpy.mockRestore();
    });

    it('removes document click listener on disconnect', async () => {
      const removeEventListenerSpy = vi.spyOn(document, 'removeEventListener');

      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.remove();

      expect(removeEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      removeEventListenerSpy.mockRestore();
    });

    it('closes menu on outside click', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      // Open menu
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Click outside
      document.body.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('does not close menu on inside click', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      // Open menu
      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      // Click inside
      const menuPopup = element.shadowRoot!.querySelector('.menu-popup') as HTMLElement;
      menuPopup.click();
      await element.updateComplete;

      expect(menuPopup.classList.contains('hidden')).toBe(false);
    });
  });

  describe('action events', () => {
    it('dispatches love event when Love button clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let eventFired = false;
      element.addEventListener('love', () => {
        eventFired = true;
      });

      const loveButton = element.shadowRoot!.querySelector('.action-button.love') as HTMLButtonElement;
      loveButton.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches ban event when Ban button clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let eventFired = false;
      element.addEventListener('ban', () => {
        eventFired = true;
      });

      const banButton = element.shadowRoot!.querySelector('.action-button.ban') as HTMLButtonElement;
      banButton.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches tired event when Snooze button clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      let eventFired = false;
      element.addEventListener('tired', () => {
        eventFired = true;
      });

      const tiredButton = element.shadowRoot!.querySelector('.action-button.tired') as HTMLButtonElement;
      tiredButton.click();

      expect(eventFired).toBe(true);
    });

    it('closes menu after Love button is clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      const loveButton = element.shadowRoot!.querySelector('.action-button.love') as HTMLButtonElement;
      loveButton.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('closes menu after Ban button is clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      const banButton = element.shadowRoot!.querySelector('.action-button.ban') as HTMLButtonElement;
      banButton.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('closes menu after Snooze button is clicked', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu></song-actions-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      const tiredButton = element.shadowRoot!.querySelector('.action-button.tired') as HTMLButtonElement;
      tiredButton.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });
  });

  describe('rating state', () => {
    it('updates active state when rating prop changes', async () => {
      const element: SongActionsMenu = await fixture(html`
        <song-actions-menu rating="0"></song-actions-menu>
      `);

      let loveButton = element.shadowRoot!.querySelector('.action-button.love');
      expect(loveButton!.classList.contains('active')).toBe(false);

      element.rating = 1;
      await element.updateComplete;

      loveButton = element.shadowRoot!.querySelector('.action-button.love');
      expect(loveButton!.classList.contains('active')).toBe(true);
    });
  });
});

