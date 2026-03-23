import { html } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Account {
  id: string;
  label: string;
}

@customElement('switch-account-modal')
export class SwitchAccountModal extends ModalBase {
  @property({ type: Array }) accounts: Account[] = [];
  @property({ type: String }) currentAccountId = '';
  @state() private selectedAccountId: string | null = null;

  constructor() {
    super();
    this.title = 'Switch Account';
  }

  protected onCancel() {
    this.selectedAccountId = null;
  }

  updated(changedProperties: Map<string, unknown>) {
    super.updated(changedProperties);
    if (changedProperties.has('open') && this.open) {
      this.selectedAccountId = this.currentAccountId;
    }
  }

  private handleSelect(accountId: string) {
    this.selectedAccountId = accountId;
  }

  private handleConfirm() {
    if (this.selectedAccountId && this.selectedAccountId !== this.currentAccountId) {
      this.dispatchEvent(new CustomEvent('account-select', {
        detail: { accountId: this.selectedAccountId }
      }));
      this.selectedAccountId = null;
    }
  }

  render() {
    const body = html`
      <div class="list-container">
        ${this.accounts.map(acct => html`
          <div
            class="list-item ${this.selectedAccountId === acct.id ? 'selected' : ''}"
            @click=${() => this.handleSelect(acct.id)}
          >
            <input
              type="radio"
              name="account"
              .checked=${this.selectedAccountId === acct.id}
              @change=${() => this.handleSelect(acct.id)}
            >
            <span class="material-icons" style="font-size: 20px;">account_circle</span>
            <span class="item-name">${acct.label}</span>
          </div>
        `)}
      </div>
    `;

    const footer = this.renderStandardFooter(
      'Switch',
      !this.selectedAccountId || this.selectedAccountId === this.currentAccountId,
      false,
      () => this.handleConfirm()
    );

    return this.renderModal(body, footer);
  }
}
