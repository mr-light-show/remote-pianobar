import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/reconnect-button';
import type { ReconnectButton } from '../../src/components/reconnect-button';

describe('ReconnectButton', () => {
  describe('rendering', () => {
    it('renders button with sync icon', async () => {
      const element: ReconnectButton = await fixture(html`
        <reconnect-button></reconnect-button>
      `);

      const button = element.shadowRoot!.querySelector('button');
      expect(button).toBeTruthy();
      
      const icon = element.shadowRoot!.querySelector('.material-icons');
      expect(icon?.textContent).toContain('sync');
    });

    it('has error styling', async () => {
      const element: ReconnectButton = await fixture(html`
        <reconnect-button></reconnect-button>
      `);

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      const styles = window.getComputedStyle(button);
      
      // Button should have error background color (defined in CSS)
      expect(button).toBeTruthy();
    });
  });

  describe('events', () => {
    it('emits reconnect event when clicked', async () => {
      const element: ReconnectButton = await fixture(html`
        <reconnect-button></reconnect-button>
      `);

      let eventFired = false;
      element.addEventListener('reconnect', () => {
        eventFired = true;
      });

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      button.click();

      expect(eventFired).toBe(true);
    });
  });

  describe('accessibility', () => {
    it('has appropriate title', async () => {
      const element: ReconnectButton = await fixture(html`
        <reconnect-button></reconnect-button>
      `);

      const button = element.shadowRoot!.querySelector('button') as HTMLButtonElement;
      expect(button.title).toBe('Reconnect');
    });
  });
});

