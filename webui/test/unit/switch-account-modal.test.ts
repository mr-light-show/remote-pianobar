import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/switch-account-modal';
import type { SwitchAccountModal } from '../../src/components/switch-account-modal';

describe('SwitchAccountModal', () => {
  const accounts = [
    { id: 'a', label: 'Account A' },
    { id: 'b', label: 'Account B' },
  ];

  it('renders one row per account', async () => {
    const el: SwitchAccountModal = await fixture(html`
      <switch-account-modal
        open
        .accounts=${accounts}
        currentAccountId="a"
      ></switch-account-modal>
    `);

    const items = el.shadowRoot!.querySelectorAll('.list-item');
    expect(items.length).toBe(2);
    expect(el.shadowRoot!.textContent).toContain('Account A');
    expect(el.shadowRoot!.textContent).toContain('Account B');
  });

  it('pre-selects current account when opened', async () => {
    const el: SwitchAccountModal = await fixture(html`
      <switch-account-modal
        open
        .accounts=${accounts}
        currentAccountId="b"
      ></switch-account-modal>
    `);

    await el.updateComplete;

    const radios = el.shadowRoot!.querySelectorAll('input[type="radio"]');
    expect((radios[0] as HTMLInputElement).checked).toBe(false);
    expect((radios[1] as HTMLInputElement).checked).toBe(true);
  });

  it('disables Switch when selection unchanged', async () => {
    const el: SwitchAccountModal = await fixture(html`
      <switch-account-modal
        open
        .accounts=${accounts}
        currentAccountId="a"
      ></switch-account-modal>
    `);

    const confirm = el.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
    expect(confirm.disabled).toBe(true);
  });

  it('clears selection when Cancel is clicked', async () => {
    const el: SwitchAccountModal = await fixture(html`
      <switch-account-modal
        open
        .accounts=${accounts}
        currentAccountId="a"
      ></switch-account-modal>
    `);

    const items = el.shadowRoot!.querySelectorAll('.list-item');
    (items[1] as HTMLElement).click();
    await el.updateComplete;

    const cancel = el.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
    cancel.click();
    await el.updateComplete;

    const confirm = el.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
    expect(confirm.disabled).toBe(true);
  });

  it('dispatches account-select when confirming another account', async () => {
    const el: SwitchAccountModal = await fixture(html`
      <switch-account-modal
        open
        .accounts=${accounts}
        currentAccountId="a"
      ></switch-account-modal>
    `);

    let detail: { accountId: string } | undefined;
    el.addEventListener('account-select', ((e: CustomEvent) => {
      detail = e.detail;
    }) as EventListener);

    const items = el.shadowRoot!.querySelectorAll('.list-item');
    (items[1] as HTMLElement).click();
    await el.updateComplete;

    const confirm = el.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
    expect(confirm.disabled).toBe(false);
    confirm.click();

    expect(detail?.accountId).toBe('b');
  });
});
