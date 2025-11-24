import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import './song-actions-menu';
import './stations-popup';
import './info-menu';

interface Station {
  id: string;
  name: string;
}

@customElement('bottom-toolbar')
export class BottomToolbar extends LitElement {
  @property({ type: Array }) stations: Station[] = [];
  @property() currentStation = '';
  @property() currentStationId = '';
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
    
    button.loved {
      color: #4CAF50;
    }
    
    button.loved:hover {
      background: rgba(76, 175, 80, 0.1);
      color: #4CAF50;
    }
    
    .button-group {
      position: relative;
      display: inline-block;
    }
    
    .material-icons,
    .material-icons-outlined {
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
    
    .material-icons {
      font-family: 'Material Icons';
    }
    
    .material-icons-outlined {
      font-family: 'Material Icons Outlined';
    }
  `;
  
  toggleTools(e: MouseEvent) {
    // Close stations menu first
    const stationsPopup = this.shadowRoot!.querySelector('stations-popup');
    if (stationsPopup) {
      (stationsPopup as any).closeMenu();
    }
    
    // Close info menu
    const infoMenu = this.shadowRoot!.querySelector('info-menu');
    if (infoMenu) {
      (infoMenu as any).closeMenu();
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
    
    // Close info menu
    const infoMenu = this.shadowRoot!.querySelector('info-menu');
    if (infoMenu) {
      (infoMenu as any).closeMenu();
    }
    
    // Then toggle stations menu
    const popup = this.shadowRoot!.querySelector('stations-popup');
    if (popup) {
      (popup as any).toggleMenu(e);
    }
  }
  
  toggleInfo(e: MouseEvent) {
    // Close tools menu first
    const toolsMenu = this.shadowRoot!.querySelector('song-actions-menu');
    if (toolsMenu) {
      (toolsMenu as any).closeMenu();
    }
    
    // Close stations menu
    const stationsPopup = this.shadowRoot!.querySelector('stations-popup');
    if (stationsPopup) {
      (stationsPopup as any).closeMenu();
    }
    
    // Then toggle info menu
    const menu = this.shadowRoot!.querySelector('info-menu');
    if (menu) {
      (menu as any).toggleMenu(e);
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
  
  handleInfoExplain() {
    this.dispatchEvent(new CustomEvent('info-explain'));
  }
  
  handleInfoUpcoming() {
    this.dispatchEvent(new CustomEvent('info-upcoming'));
  }
  
  handleInfoQuickMix() {
    this.dispatchEvent(new CustomEvent('info-quickmix'));
  }
  
  handleInfoCreateStation() {
    this.dispatchEvent(new CustomEvent('info-create-station'));
  }
  
  handleInfoDeleteStation() {
    this.dispatchEvent(new CustomEvent('info-delete-station'));
  }
  
  handleInfoAddGenre() {
    this.dispatchEvent(new CustomEvent('info-add-genre'));
  }
  
  render() {
    return html`
      <div class="button-group">
        <button 
          @click=${this.toggleTools}
          class="${this.rating === 1 ? 'loved' : ''}"
          title="${this.rating === 1 ? 'Loved' : 'Rate this song'}"
        >
          <span class="${this.rating === 1 ? 'material-icons' : 'material-icons-outlined'}">
            ${this.rating === 1 ? 'thumb_up' : 'thumbs_up_down'}
          </span>
        </button>
        <song-actions-menu
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
        ></song-actions-menu>
      </div>
      
      <div class="button-group">
        <button @click=${this.toggleInfo} title="Song info">
          <span class="material-icons">info_outline</span>
        </button>
        <info-menu
          @info-explain=${this.handleInfoExplain}
          @info-upcoming=${this.handleInfoUpcoming}
          @info-quickmix=${this.handleInfoQuickMix}
          @info-create-station=${this.handleInfoCreateStation}
          @info-add-genre=${this.handleInfoAddGenre}
          @info-delete-station=${this.handleInfoDeleteStation}
        ></info-menu>
      </div>
      
      <div class="button-group">
        <button @click=${this.toggleStations}>
          <span class="material-icons">radio</span>
          <span>Stations</span>
        </button>
        <stations-popup
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          currentStationId="${this.currentStationId}"
          @station-change=${this.handleStationChange}
        ></stations-popup>
      </div>
    `;
  }
}

