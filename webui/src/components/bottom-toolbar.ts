import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import './song-actions-menu';
import './stations-popup';

interface Station {
  id: string;
  name: string;
}

@customElement('bottom-toolbar')
export class BottomToolbar extends LitElement {
  @property({ type: Array }) stations: Station[] = [];
  @property() currentStation = '';
  @property({ type: Number }) rating = 0;
  
  static styles = css`
    :host {
      position: fixed;
      bottom: 0;
      left: 0;
      right: 0;
      background: var(--surface);
      border-top: 1px solid var(--outline);
      padding: 12px 16px;
      display: flex;
      justify-content: center;
      gap: 16px;
      z-index: 50;
      box-shadow: 0 -2px 8px rgba(0, 0, 0, 0.1);
    }
    
    button {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 10px 20px;
      border: none;
      border-radius: 20px;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      transition: all 0.2s;
      font-size: 14px;
      font-weight: 500;
    }
    
    button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    .button-group {
      position: relative;
      display: inline-block;
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
    }
  `;
  
  toggleTools(e: MouseEvent) {
    // Close stations menu first
    const stationsPopup = this.shadowRoot!.querySelector('stations-popup');
    if (stationsPopup) {
      (stationsPopup as any).closeMenu();
    }
    
    // Then toggle tools menu
    const menu = this.shadowRoot!.querySelector('song-actions-menu');
    if (menu) {
      (menu as any).toggleMenu(e);
    }
  }
  
  toggleStations(e: MouseEvent) {
    // Close tools menu first
    const toolsMenu = this.shadowRoot!.querySelector('song-actions-menu');
    if (toolsMenu) {
      (toolsMenu as any).closeMenu();
    }
    
    // Then toggle stations menu
    const popup = this.shadowRoot!.querySelector('stations-popup');
    if (popup) {
      (popup as any).toggleMenu(e);
    }
  }
  
  handleLove() {
    this.dispatchEvent(new CustomEvent('love'));
  }
  
  handleBan() {
    this.dispatchEvent(new CustomEvent('ban'));
  }
  
  handleTired() {
    this.dispatchEvent(new CustomEvent('tired'));
  }
  
  handleStationChange(e: CustomEvent) {
    this.dispatchEvent(new CustomEvent('station-change', {
      detail: e.detail
    }));
  }
  
  render() {
    return html`
      <div class="button-group">
        <button @click=${this.toggleTools}>
          <span class="material-icons">tune</span>
          <span>Tools</span>
        </button>
        <song-actions-menu
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
        ></song-actions-menu>
      </div>
      
      <div class="button-group">
        <button @click=${this.toggleStations}>
          <span class="material-icons">radio</span>
          <span>Stations</span>
        </button>
        <stations-popup
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          @station-change=${this.handleStationChange}
        ></stations-popup>
      </div>
    `;
  }
}

