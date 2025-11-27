import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

interface Station {
  id: string;
  name: string;
}

@customElement('stations-menu')
export class StationsMenu extends LitElement {
  @property({ type: Array }) stations: Station[] = [];
  @property() currentStation = '';
  
  static styles = css`
    :host {
      display: block;
      padding: 1rem;
    }
    
    button {
      display: block;
      width: 100%;
      padding: 0.75rem 1rem;
      margin: 0.25rem 0;
      border: 1px solid var(--outline);
      border-radius: 8px;
      background: var(--surface);
      color: var(--on-surface);
      text-align: left;
      cursor: pointer;
      transition: all 0.2s;
    }
    
    button:hover {
      background: var(--surface-variant);
    }
    
    button.active {
      background: var(--primary-container);
      color: var(--on-primary-container);
      border-color: var(--primary-color);
    }
  `;
  
  handleStationClick(station: Station) {
    this.dispatchEvent(new CustomEvent('station-change', {
      detail: { station: station.id }
    }));
  }
  
  render() {
    return html`
      ${this.stations.map(station => html`
        <button 
          class="${station.id === this.currentStation ? 'active' : ''}"
          @click=${() => this.handleStationClick(station)}
        >
          ${station.name}
        </button>
      `)}
    `;
  }
}

