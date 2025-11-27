import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/toast-notification';
import type { ToastNotification } from '../../src/components/toast-notification';

describe('ToastNotification', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  describe('rendering', () => {
    it('renders with message prop', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test message"></toast-notification>
      `);

      const toast = element.shadowRoot!.querySelector('.toast');
      expect(toast).toBeTruthy();
      expect(toast!.textContent).toContain('Test message');
    });

    it('renders with custom content', async () => {
      const customContent = html`<div class="custom">Custom Content</div>`;
      const element: ToastNotification = await fixture(html`
        <toast-notification .content=${customContent}></toast-notification>
      `);

      const toast = element.shadowRoot!.querySelector('.toast');
      expect(toast).toBeTruthy();
      expect(toast!.textContent).toContain('Custom Content');
    });

    it('starts hidden and becomes visible after 10ms', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      const toast = element.shadowRoot!.querySelector('.toast');
      expect(toast!.classList.contains('visible')).toBe(false);

      await vi.advanceTimersByTimeAsync(10);
      await element.updateComplete;

      expect(toast!.classList.contains('visible')).toBe(true);
    });
  });

  describe('auto-dismiss', () => {
    it('auto-dismisses after 5000ms by default', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      // Fast-forward past the 5000ms duration plus animation time
      await vi.advanceTimersByTimeAsync(5300);
      await element.updateComplete;

      expect(dismissFired).toBe(true);
    });

    it('respects custom duration', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test" duration="2000"></toast-notification>
      `);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      // Should not dismiss before duration
      await vi.advanceTimersByTimeAsync(1500);
      expect(dismissFired).toBe(false);

      // Should dismiss after duration plus animation time
      await vi.advanceTimersByTimeAsync(800);
      await element.updateComplete;
      expect(dismissFired).toBe(true);
    });

    it('does not auto-dismiss when duration is 0', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test" duration="0"></toast-notification>
      `);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      await vi.advanceTimersByTimeAsync(10000);

      expect(dismissFired).toBe(false);
    });
  });

  describe('click dismissal', () => {
    it('enables click dismissal after 250ms delay', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      // Should not dismiss immediately
      document.body.click();
      await vi.advanceTimersByTimeAsync(0);
      
      const toast = element.shadowRoot!.querySelector('.toast');
      expect(toast).toBeTruthy(); // Still present

      // Should dismiss after 250ms delay
      await vi.advanceTimersByTimeAsync(250);
      
      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });
      
      document.body.click();
      await vi.advanceTimersByTimeAsync(300);
      await element.updateComplete;

      expect(dismissFired).toBe(true);
    });

    it('does not dismiss when clicking inside toast', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      await vi.advanceTimersByTimeAsync(250);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      const toast = element.shadowRoot!.querySelector('.toast') as HTMLElement;
      toast.click();
      await vi.advanceTimersByTimeAsync(0);

      expect(dismissFired).toBe(false);
    });

    it('dismisses when clicking outside toast', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      await vi.advanceTimersByTimeAsync(250);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      document.body.click();
      await vi.advanceTimersByTimeAsync(300);
      await element.updateComplete;

      expect(dismissFired).toBe(true);
    });
  });

  describe('dismiss method', () => {
    it('sets visible to false when dismissed', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      await vi.advanceTimersByTimeAsync(10);
      await element.updateComplete;

      const toast = element.shadowRoot!.querySelector('.toast');
      expect(toast!.classList.contains('visible')).toBe(true);

      element.dismiss();
      await element.updateComplete;

      expect(toast!.classList.contains('visible')).toBe(false);
    });

    it('dispatches dismiss event', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      element.dismiss();
      await vi.advanceTimersByTimeAsync(300); // Wait for animation

      expect(dismissFired).toBe(true);
    });

    it('dispatches dismiss event when dismissed', async () => {
      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      let dismissFired = false;
      element.addEventListener('dismiss', () => {
        dismissFired = true;
      });

      element.dismiss();
      await vi.advanceTimersByTimeAsync(300);
      await element.updateComplete;

      expect(dismissFired).toBe(true);
    });
  });

  describe('lifecycle', () => {
    it('adds document click listener on connect', async () => {
      const addEventListenerSpy = vi.spyOn(document, 'addEventListener');

      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      await vi.advanceTimersByTimeAsync(250);

      expect(addEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      addEventListenerSpy.mockRestore();
    });

    it('removes document click listener on disconnect', async () => {
      const removeEventListenerSpy = vi.spyOn(document, 'removeEventListener');

      const element: ToastNotification = await fixture(html`
        <toast-notification message="Test"></toast-notification>
      `);

      element.remove();

      expect(removeEventListenerSpy).toHaveBeenCalledWith('click', expect.any(Function));

      removeEventListenerSpy.mockRestore();
    });
  });
});

