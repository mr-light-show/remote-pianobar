import { LitElement, html, css } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('reconnect-button')
export class ReconnectButton extends LitElement {
  static styles = css`
    :host {
      display: flex;
      justify-content: center;
      margin: 2rem 0;
    }
    
    button {
      width: 56px;
      height: 56px;
      border-radius: 50%;
      border: none;
      background: var(--error);
      color: var(--on-error);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
      animation: pulse 2s infinite;
    }
    
    button:hover {
      background: var(--error-dark);
      animation: none;
    }
    
    @keyframes pulse {
      0%, 100% {
        opacity: 1;
      }
      50% {
        opacity: 0.6;
      }
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 28px;
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
  `;
  
  handleReconnect() {
    this.dispatchEvent(new CustomEvent('reconnect'));
  }
  
  render() {
    return html`
      <button @click=${this.handleReconnect} title="Reconnect">
        <span class="material-icons">sync</span>
      </button>
    `;
  }
}

