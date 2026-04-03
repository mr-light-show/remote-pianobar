import { LitElement, html, css } from 'lit';
import { customElement } from 'lit/decorators.js';
import { t } from '../i18n';

@customElement('login-screen')
export class LoginScreen extends LitElement {
  static styles = css`
    :host {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 2rem;
      text-align: center;
      min-height: 300px;
    }
    
    .icon {
      font-size: 64px;
      color: var(--on-surface-variant);
      margin-bottom: 1.5rem;
      opacity: 0.7;
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: inherit;
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
    
    h2 {
      margin: 0 0 0.5rem 0;
      font-size: 1.5rem;
      font-weight: 500;
      color: var(--on-surface);
    }
    
    .description {
      color: var(--on-surface-variant);
      margin: 0 0 2rem 0;
      font-size: 0.95rem;
      max-width: 280px;
      line-height: 1.5;
    }
    
    button {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      padding: 0.875rem 1.75rem;
      border-radius: 24px;
      border: none;
      background: var(--primary-color);
      color: var(--on-primary);
      cursor: pointer;
      font-size: 1rem;
      font-weight: 500;
      transition: all 0.2s;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
    }
    
    button:hover {
      background: var(--primary-dark);
      transform: translateY(-1px);
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    }
    
    button:active {
      transform: translateY(0);
    }
    
    button .material-icons {
      font-size: 20px;
    }
  `;
  
  handleReconnect() {
    this.dispatchEvent(new CustomEvent('pandora-reconnect'));
  }
  
  render() {
    return html`
      <span class="icon material-icons">cloud_off</span>
      <h2>${t('web.ui.login_screen_heading')}</h2>
      <p class="description">
        ${t('web.ui.login_screen_body')}
      </p>
      <button @click=${this.handleReconnect}>
        <span class="material-icons">login</span>
        ${t('web.ui.login_screen_button')}
      </button>
    `;
  }
}

