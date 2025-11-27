import { LitElement, html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';

interface Station {
  id: string;
  name: string;
  isQuickMix?: boolean;
  isQuickMixed?: boolean;
}

@customElement('stations-popup')
export class StationsPopup extends LitElement {
  @property({ type: Array }) stations: Station[] = [];
  @property() currentStation = '';
  @property() currentStationId = '';
  @state() private menuOpen = false;
  
  connectedCallback() {
    super.connectedCallback();
    // Listen for clicks on document to close menu
    document.addEventListener('click', this.handleClickOutside);
  }
  
  disconnectedCallback() {
    super.disconnectedCallback();
    // Clean up listener
    document.removeEventListener('click', this.handleClickOutside);
  }
  
  private handleClickOutside = (event: MouseEvent) => {
    // Check if click is outside this component
    if (this.menuOpen && !event.composedPath().includes(this)) {
      this.menuOpen = false;
    }
  };
  
  static styles = css`
    :host {
      position: relative;
      display: inline-block;
    }
    
    .menu-popup {
      position: absolute;
      bottom: 64px;
      left: 50%;
      transform: translateX(-50%);
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      padding: 8px;
      max-height: 400px;
      overflow-y: auto;
      z-index: 100;
      min-width: 250px;
      max-width: 90vw;
    }
    
    .menu-popup.hidden {
      display: none;
    }
    
    .station-button {
      display: flex;
      align-items: center;
      gap: 8px;
      width: 100%;
      padding: 12px 16px;
      margin: 2px 0;
      border: none;
      border-radius: 8px;
      background: transparent;
      color: var(--on-surface);
      text-align: left;
      cursor: pointer;
      transition: all 0.2s;
      font-size: 14px;
    }
    
    .station-button:hover {
      background: var(--surface-variant);
    }
    
    .station-button.active {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    .station-button.active:hover {
      background: var(--primary-container);
    }
    
    .station-icon-leading {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
      line-height: 1;
      color: var(--on-surface);
      flex-shrink: 0;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    .station-name {
      flex: 1;
      text-align: left;
    }
    
    .quickmix-icon {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 18px;
      line-height: 1;
      margin-left: auto;
      color: var(--primary-color);
      flex-shrink: 0;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;
  
  toggleMenu() {
    // Delay toggle to ensure it happens after current click event completes
    // This prevents the document click listener from immediately closing the menu
    setTimeout(() => {
      this.menuOpen = !this.menuOpen;
    }, 0);
  }
  
  closeMenu() {
    this.menuOpen = false;
  }
  
  handleStationClick(station: Station) {
    this.dispatchEvent(new CustomEvent('station-change', {
      detail: { station: station.id }
    }));
    this.closeMenu();
  }
  
  render() {
    return html`
      <div class="menu-popup ${this.menuOpen ? '' : 'hidden'}">
        ${this.stations.map(station => {
          const leadingIcon = station.isQuickMix ? 'shuffle' : 'play_arrow';
          const showTrailingIcon = station.isQuickMixed && !station.isQuickMix;
          
          return html`
            <button 
              class="station-button ${station.id === this.currentStationId ? 'active' : ''}"
              @click=${() => this.handleStationClick(station)}
            >
              <span class="material-icons station-icon-leading">${leadingIcon}</span>
              <span class="station-name">${station.name}</span>
              ${showTrailingIcon ? html`<span class="material-icons quickmix-icon">shuffle</span>` : ''}
            </button>
          `;
        })}
      </div>
    `;
  }
}

