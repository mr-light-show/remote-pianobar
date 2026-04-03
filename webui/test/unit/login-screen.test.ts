import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/login-screen';
import type { LoginScreen } from '../../src/components/login-screen';

describe('LoginScreen', () => {
  it('renders localized heading, body, and button from default locale', async () => {
    const el: LoginScreen = await fixture(html`<login-screen></login-screen>`);
    await el.updateComplete;

    const h2 = el.shadowRoot!.querySelector('h2');
    const body = el.shadowRoot!.querySelector('.description');
    const button = el.shadowRoot!.querySelector('button');

    expect(h2?.textContent).toBe('Not Connected to Pandora');
    expect(body?.textContent).toContain('disconnected from Pandora');
    expect(button?.textContent).toContain('Reconnect to Pandora');
  });

  it('dispatches pandora-reconnect when button is clicked', async () => {
    const el: LoginScreen = await fixture(html`<login-screen></login-screen>`);
    await el.updateComplete;

    let fired = false;
    el.addEventListener('pandora-reconnect', () => {
      fired = true;
    });

    const button = el.shadowRoot!.querySelector('button') as HTMLButtonElement;
    button.click();
    expect(fired).toBe(true);
  });
});
