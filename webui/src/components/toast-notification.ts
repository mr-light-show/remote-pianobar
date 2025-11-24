import { LitElement, html, css, TemplateResult } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';

@customElement('toast-notification')
export class ToastNotification extends LitElement {
  @property({ type: String }) message = '';
  @property({ type: Object }) content: TemplateResult | null = null;
  @property({ type: Number }) duration = 5000;
  @state() private visible = false;
  
  private timeoutId: number | null = null;
  private clickDismissEnabled = false;
  
  connectedCallback() {
    super.connectedCallback();
    
    // Show with animation
    setTimeout(() => {
      this.visible = true;
    }, 10);
    
    // Enable click-to-dismiss after 250ms to prevent modal close click from dismissing
    setTimeout(() => {
      this.clickDismissEnabled = true;
      document.addEventListener('click', this.handleClickOutside);
    }, 250);
    
    // Auto-dismiss after duration
    if (this.duration > 0) {
      this.timeoutId = window.setTimeout(() => {
        this.dismiss();
      }, this.duration);
    }
  }
  
  disconnectedCallback() {
    super.disconnectedCallback();
    document.removeEventListener('click', this.handleClickOutside);
    if (this.timeoutId !== null) {
      clearTimeout(this.timeoutId);
    }
  }
  
  private handleClickOutside = (event: MouseEvent) => {
    // Only dismiss if click-to-dismiss is enabled and click is outside
    if (this.clickDismissEnabled && !event.composedPath().includes(this)) {
      this.dismiss();
    }
  };
  
  dismiss() {
    this.visible = false;
    // Wait for animation to complete before removing
    setTimeout(() => {
      this.dispatchEvent(new CustomEvent('dismiss'));
      this.remove();
    }, 300);
  }
  
  static styles = css`
    :host {
      position: fixed;
      bottom: 80px;
      left: 50%;
      transform: translateX(-50%);
      z-index: 1000;
      max-width: 90%;
      width: auto;
      pointer-events: none;
    }
    
    .toast {
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
      padding: 16px 20px;
      color: var(--on-surface);
      font-size: 14px;
      line-height: 1.5;
      max-width: 600px;
      max-height: 70vh;
      overflow-y: auto;
      opacity: 0;
      transform: translateY(20px);
      transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
      pointer-events: all;
      border: 1px solid var(--outline);
    }
    
    .toast.visible {
      opacity: 1;
      transform: translateY(0);
    }
    
    .toast-content {
      margin: 0;
    }
    
    .song-list {
      display: flex;
      flex-direction: column;
      gap: 12px;
      margin-top: 8px;
    }
    
    .song-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 8px;
      background: var(--surface-variant);
      border-radius: 8px;
    }
    
    .song-number {
      font-weight: 600;
      color: var(--primary);
      min-width: 20px;
    }
    
    .song-cover {
      width: 40px;
      height: 40px;
      border-radius: 4px;
      object-fit: cover;
      background: var(--surface-container);
    }
    
    .song-details {
      flex: 1;
      min-width: 0;
    }
    
    .song-title {
      font-weight: 500;
      margin: 0 0 2px 0;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    
    .song-artist {
      font-size: 12px;
      color: var(--on-surface-variant);
      margin: 0;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    
    .song-station {
      font-size: 11px;
      color: var(--tertiary);
      margin: 2px 0 0 0;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    
    .song-duration {
      font-size: 12px;
      color: var(--on-surface-variant);
      white-space: nowrap;
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
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
      color: #4CAF50;
    }
    
    /* Scrollbar styling */
    .toast::-webkit-scrollbar {
      width: 8px;
    }
    
    .toast::-webkit-scrollbar-track {
      background: transparent;
    }
    
    .toast::-webkit-scrollbar-thumb {
      background: var(--outline);
      border-radius: 4px;
    }
    
    .toast::-webkit-scrollbar-thumb:hover {
      background: var(--on-surface-variant);
    }
  `;
  
  render() {
    return html`
      <div class="toast ${this.visible ? 'visible' : ''}">
        ${this.content || html`<p class="toast-content">${this.message}</p>`}
      </div>
    `;
  }
}

