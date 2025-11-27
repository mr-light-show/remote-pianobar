import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import './stations-popup';

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
  @property() songStationName = '';
  
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
  
  toggleStations(e: MouseEvent) {
    // Toggle stations menu
    const popup = this.shadowRoot!.querySelector('stations-popup');
    if (popup) {
      (popup as any).toggleMenu(e);
    }
  }
  
  handleStationChange(e: CustomEvent) {
    this.dispatchEvent(new CustomEvent('station-change', {
      detail: e.detail
    }));
  }
  
  render() {
    // Find current station object to check if it's QuickMix
    const currentStationObj = this.stations.find(s => s.id === this.currentStationId);
    const isQuickMix = currentStationObj?.isQuickMix || false;
    
    // Determine what to display on the button
    let stationDisplayName = this.currentStation || 'Select Station';
    
    if (isQuickMix && this.songStationName) {
      // If it's QuickMix, show the song's station name
      stationDisplayName = this.songStationName;
    }
    
    return html`
      <div class="button-group">
        <button @click=${this.toggleStations} title="Select Station">
          <span class="material-icons">radio</span>
          <span>${stationDisplayName}</span>
          ${isQuickMix ? html`<span class="material-icons">shuffle</span>` : ''}
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

