import { LitElement, css, html } from 'lit';
import { property } from 'lit/decorators.js';

export class ModalBase extends LitElement {
  @property({ type: Boolean }) open = false;
  @property({ type: String }) title = '';
  
  handleBackdropClick(e: MouseEvent) {
    if (e.target === e.currentTarget) {
      this.handleCancel();
    }
  }
  
  /**
   * Override this in subclasses to reset state when modal is cancelled
   */
  protected onCancel() {
    // Subclasses can override
  }
  
  handleCancel() {
    this.onCancel();
    this.dispatchEvent(new CustomEvent('cancel'));
  }
  
  protected renderLoading(message = 'Loading...') {
    return html`<div class="loading">${message}</div>`;
  }
  
  protected renderStandardFooter(
    confirmText: string,
    confirmDisabled: boolean = false,
    confirmDanger: boolean = false,
    onConfirm: () => void
  ) {
    return html`
      <div class="modal-footer">
        <button class="button-cancel" @click=${this.handleCancel}>
          Cancel
        </button>
        <button 
          class="button-confirm ${confirmDanger ? 'button-danger' : ''}" 
          ?disabled=${confirmDisabled}
          @click=${onConfirm}
        >
          ${confirmText}
        </button>
      </div>
    `;
  }
  
  protected renderModal(bodyContent: any, footerContent?: any) {
    return html`
      <div class="backdrop" @click=${this.handleBackdropClick}></div>
      <div class="modal">
        ${this.title ? html`
          <div class="modal-header">
            <h2 class="modal-title">${this.title}</h2>
          </div>
        ` : ''}
        <div class="modal-body">
          ${bodyContent}
        </div>
        ${footerContent ? footerContent : ''}
      </div>
    `;
  }
  
  static styles = [css`
    :host {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      z-index: 2000;
      align-items: center;
      justify-content: center;
    }
    
    :host([open]) {
      display: flex;
    }
    
    .backdrop {
      position: absolute;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: rgba(0, 0, 0, 0.6);
      backdrop-filter: blur(4px);
    }
    
    .modal {
      position: relative;
      background: var(--surface);
      border-radius: 16px;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
      max-width: 500px;
      width: 90%;
      max-height: 80vh;
      display: flex;
      flex-direction: column;
      border: 1px solid var(--outline);
      overflow: hidden;
    }
    
    .modal-header {
      padding: 20px 24px;
      border-bottom: 1px solid var(--outline);
    }
    
    .modal-title {
      margin: 0;
      font-size: 20px;
      font-weight: 500;
      color: var(--on-surface);
    }
    
    .modal-body {
      padding: 16px 24px;
      overflow-y: auto;
      flex: 1;
      min-height: 0;
    }
    
    .modal-footer {
      padding: 16px 24px;
      border-top: 1px solid var(--outline);
      display: flex;
      gap: 12px;
      justify-content: flex-end;
    }
    
    button {
      padding: 10px 24px;
      border: none;
      border-radius: 20px;
      font-size: 14px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s;
    }
    
    .button-cancel {
      background: var(--surface-variant);
      color: var(--on-surface);
    }
    
    .button-cancel:hover {
      background: var(--surface-container-high);
    }
    
    .button-confirm,
    .button-save {
      background: var(--primary-container);
      color: var(--on-primary-container);
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
    }
    
    .button-confirm:hover:not(:disabled),
    .button-save:hover:not(:disabled) {
      background: var(--primary-container);
      color: var(--on-primary-container);
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
    }
    
    .button-confirm:active:not(:disabled),
    .button-save:active:not(:disabled) {
      transform: translateY(0);
      box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
    }
    
    .button-confirm:disabled,
    .button-save:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    
    .button-confirm.button-danger {
      background: var(--error);
      color: var(--on-error);
    }
    
    .button-confirm.button-danger:hover:not(:disabled) {
      background: var(--error-container);
      color: var(--on-error-container);
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
    }
    
    .button-confirm.button-danger:active:not(:disabled) {
      transform: translateY(0);
      box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
    }
    
    /* Radio and checkbox inputs */
    input[type="radio"],
    input[type="checkbox"] {
      width: 15px;
      height: 15px;
      cursor: pointer;
      accent-color: var(--primary);
    }
    
    /* Scrollbar styling */
    .modal-body::-webkit-scrollbar {
      width: 8px;
    }
    
    .modal-body::-webkit-scrollbar-track {
      background: transparent;
    }
    
    .modal-body::-webkit-scrollbar-thumb {
      background: var(--outline);
      border-radius: 4px;
    }
    
    .modal-body::-webkit-scrollbar-thumb:hover {
      background: var(--on-surface-variant);
    }
    
    /* Material Icons support */
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 24px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    /* Common list/item patterns */
    .list-container {
      display: flex;
      flex-direction: column;
      gap: 8px;
    }
    
    .list-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: var(--surface-variant);
      border-radius: 8px;
      cursor: pointer;
      transition: background 0.2s;
      user-select: none;
    }
    
    .list-item:hover {
      background: var(--surface-container-high);
    }
    
    .list-item.selected {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    .list-item label {
      display: flex;
      align-items: center;
      gap: 12px;
      cursor: pointer;
      flex: 1;
      user-select: none;
    }
    
    .item-name {
      font-size: 14px;
      color: var(--on-surface);
    }
    
    .list-item.selected .item-name {
      color: var(--on-primary-container);
    }
    
    /* Loading state */
    .loading {
      text-align: center;
      padding: 40px;
      color: var(--on-surface-variant);
      font-size: 16px;
    }
  `];
}

