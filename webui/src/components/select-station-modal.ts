import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Station {
  id: string;
  name: string;
  isQuickMix: boolean;
}

@customElement('select-station-modal')
export class SelectStationModal extends ModalBase {
  @property({ type: String }) confirmText = 'Select';
  @property({ type: Boolean }) confirmDanger = false;
  @property({ type: Array }) stations: Station[] = [];
  @property({ type: Boolean}) excludeQuickMix = false;
  @state() private selectedStationId: string | null = null;
  
  constructor() {
    super();
    this.title = 'Select Station';
  }
  
  handleStationSelect(stationId: string) {
    this.selectedStationId = stationId;
  }
  
  handleConfirm() {
    if (this.selectedStationId) {
      this.dispatchEvent(new CustomEvent('station-select', {
        detail: { stationId: this.selectedStationId }
      }));
      this.selectedStationId = null;
    }
  }
  
  protected onCancel() {
    this.selectedStationId = null;
  }
  
  static styles = [
    ModalBase.styles,
    css`
    .modal {
      max-height: 60vh;
    }
  `
  ];
  
  render() {
    const selectableStations = this.excludeQuickMix 
      ? this.stations.filter(s => !s.isQuickMix)
      : this.stations;
      
    const body = html`
      <div class="list-container">
        ${selectableStations.length === 0 ? html`<p>No stations available.</p>` : ''}
        ${selectableStations.map(station => html`
          <div 
            class="list-item ${this.selectedStationId === station.id ? 'selected' : ''}"
            @click=${() => this.handleStationSelect(station.id)}
          >
            <input
              type="radio"
              name="station"
              .checked=${this.selectedStationId === station.id}
              @change=${() => this.handleStationSelect(station.id)}
            >
            <span class="item-name">${station.name}</span>
          </div>
        `)}
      </div>
    `;
    
    const footer = this.renderStandardFooter(
      this.confirmText,
      !this.selectedStationId,
      this.confirmDanger,
      () => this.handleConfirm()
    );
    
    return this.renderModal(body, footer);
  }
}

