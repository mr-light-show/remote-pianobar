import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/info-menu';
import type { InfoMenu } from '../../src/components/info-menu';

describe('InfoMenu', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  describe('rendering', () => {
    it('starts with menu closed', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });

    it('renders all action buttons', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const actionButtons = element.shadowRoot!.querySelectorAll('.action-button');
      expect(actionButtons.length).toBe(9); // All menu items
    });

    it('renders explain action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Explain why this song is playing');
    });

    it('renders upcoming action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Show upcoming songs');
    });

    it('renders station mode action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Manage Station Mode');
    });

    it('renders station seeds action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Manage Seeds & Feedback');
    });

    it('renders quickmix action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Set QuickMix stations');
    });

    it('renders create station action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Create station');
    });

    it('renders add music action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Add music to station');
    });

    it('renders rename station action', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const content = element.shadowRoot!.querySelector('.menu-popup');
      expect(content!.textContent).toContain('Rename station');
    });

    it('renders delete station action with danger styling', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const actionButtons = element.shadowRoot!.querySelectorAll('.action-button');
      const deleteButton = Array.from(actionButtons).find(btn =>
        btn.textContent?.includes('Delete station')
      );

      expect(deleteButton).toBeTruthy();
      expect(deleteButton!.classList.contains('delete')).toBe(true);
    });

    it('renders menu divider', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      const divider = element.shadowRoot!.querySelector('.menu-divider');
      expect(divider).toBeTruthy();
    });
  });

  describe('menu toggle', () => {
    it('uses setTimeout(0) pattern for toggle', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
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
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
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

      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      expect(addEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      addEventListenerSpy.mockRestore();
    });

    it('removes document click listener on disconnect', async () => {
      const removeEventListenerSpy = vi.spyOn(document, 'removeEventListener');

      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      element.remove();

      expect(removeEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      removeEventListenerSpy.mockRestore();
    });

    it('closes menu on outside click', async () => {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
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
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
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
    async function openMenuAndGetButton(buttonText: string): Promise<[InfoMenu, HTMLButtonElement]> {
      const element: InfoMenu = await fixture(html`
        <info-menu></info-menu>
      `);

      element.toggleMenu();
      await vi.advanceTimersByTimeAsync(0);
      await element.updateComplete;

      const actionButtons = element.shadowRoot!.querySelectorAll('.action-button');
      const button = Array.from(actionButtons).find(btn =>
        btn.textContent?.includes(buttonText)
      ) as HTMLButtonElement;

      return [element, button];
    }

    it('dispatches info-explain event', async () => {
      const [element, button] = await openMenuAndGetButton('Explain');

      let eventFired = false;
      element.addEventListener('info-explain', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-upcoming event', async () => {
      const [element, button] = await openMenuAndGetButton('upcoming');

      let eventFired = false;
      element.addEventListener('info-upcoming', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-quickmix event', async () => {
      const [element, button] = await openMenuAndGetButton('QuickMix');

      let eventFired = false;
      element.addEventListener('info-quickmix', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-create-station event', async () => {
      const [element, button] = await openMenuAndGetButton('Create station');

      let eventFired = false;
      element.addEventListener('info-create-station', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-add-music event', async () => {
      const [element, button] = await openMenuAndGetButton('Add music');

      let eventFired = false;
      element.addEventListener('info-add-music', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-rename-station event', async () => {
      const [element, button] = await openMenuAndGetButton('Rename');

      let eventFired = false;
      element.addEventListener('info-rename-station', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-station-mode event', async () => {
      const [element, button] = await openMenuAndGetButton('Station Mode');

      let eventFired = false;
      element.addEventListener('info-station-mode', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-station-seeds event', async () => {
      const [element, button] = await openMenuAndGetButton('Seeds');

      let eventFired = false;
      element.addEventListener('info-station-seeds', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('dispatches info-delete-station event', async () => {
      const [element, button] = await openMenuAndGetButton('Delete');

      let eventFired = false;
      element.addEventListener('info-delete-station', () => {
        eventFired = true;
      });

      button.click();

      expect(eventFired).toBe(true);
    });

    it('closes menu after action is clicked', async () => {
      const [element, button] = await openMenuAndGetButton('Explain');

      button.click();
      await element.updateComplete;

      const menuPopup = element.shadowRoot!.querySelector('.menu-popup');
      expect(menuPopup!.classList.contains('hidden')).toBe(true);
    });
  });
});

