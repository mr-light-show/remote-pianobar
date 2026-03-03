import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

@customElement('album-art')
export class AlbumArt extends LitElement {
  @property() src = '';
  @property() alt = 'Album Art';
  @property({ type: Boolean }) showPandoraReconnect = false;
  @property({ type: Boolean }) showWebsocketReconnect = false;
  
  static styles = css`
    :host {
      display: block;
      max-width: clamp(16rem, 85vw, 32rem);
      margin: 0 auto;
      aspect-ratio: 1 / 1;
    }
    
    img {
      width: 100%;
      height: 100%;
      object-fit: cover;
    }
    
    .disc-icon {
      width: 100%;
      height: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
      background: var(--surface);
      color: var(--on-surface);
    }
    
    .reconnect-panel {
      width: 100%;
      height: 100%;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      background: var(--surface);
      color: var(--on-surface);
      text-align: center;
      padding: 1.5rem;
      box-sizing: border-box;
    }
    
    .reconnect-panel .icon {
      font-size: clamp(3rem, 15vw, 5rem);
      color: var(--on-surface-variant);
      margin-bottom: 1rem;
      opacity: 0.7;
    }
    
    .reconnect-panel.error .icon {
      color: var(--error);
      opacity: 0.8;
    }
    
    .reconnect-panel h3 {
      margin: 0 0 0.5rem 0;
      font-size: clamp(1rem, 4vw, 1.25rem);
      font-weight: 500;
    }
    
    .reconnect-panel p {
      margin: 0 0 1.25rem 0;
      font-size: clamp(0.8rem, 3vw, 0.95rem);
      color: var(--on-surface-variant);
      line-height: 1.4;
    }
    
    .reconnect-panel button {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      padding: 0.75rem 1.5rem;
      border-radius: 24px;
      border: none;
      background: var(--primary-color);
      color: var(--on-primary);
      cursor: pointer;
      font-size: clamp(0.85rem, 3vw, 1rem);
      font-weight: 500;
      transition: all 0.2s;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
    }
    
    .reconnect-panel button:hover {
      background: var(--primary-dark);
      transform: translateY(-1px);
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    }
    
    .reconnect-panel button .material-icons {
      font-size: 18px;
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: clamp(4rem, 20vw, 8rem);
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
  
  private handlePandoraReconnect() {
    this.dispatchEvent(new CustomEvent('pandora-reconnect'));
  }
  
  private handleWebsocketReconnect() {
    this.dispatchEvent(new CustomEvent('websocket-reconnect'));
  }
  
  render() {
    // WebSocket disconnected takes priority
    if (this.showWebsocketReconnect) {
      return html`
        <div class="reconnect-panel error">
          <span class="icon material-icons">sync_problem</span>
          <h3>Connection Lost</h3>
          <p>Unable to connect to pianobar server.</p>
          <button @click=${this.handleWebsocketReconnect} title="Reconnect">
            <span class="material-icons">sync</span>
            Reconnect
          </button>
        </div>
      `;
    }
    
    // Pandora disconnected
    if (this.showPandoraReconnect) {
      return html`
        <div class="reconnect-panel">
          <span class="icon material-icons">cloud_off</span>
          <h3>Not Connected to Pandora</h3>
          <p>Click below to reconnect and access your stations.</p>
          <button @click=${this.handlePandoraReconnect} title="Reconnect">
            <span class="material-icons">login</span>
            Reconnect
          </button>
        </div>
      `;
    }
    
    return this.src
      ? html`<img src="${this.src}" alt="${this.alt}">`
      : html`<div class="disc-icon">
               <span class="material-icons">album</span>
             </div>`;
  }
}

