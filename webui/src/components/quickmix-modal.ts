import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Station {
  id: string;
  name: string;
  isQuickMix: boolean;
  isQuickMixed: boolean;
}

@customElement('quickmix-modal')
export class QuickMixModal extends ModalBase {
  @property({ type: Array }) stations: Station[] = [];
  @state() private selectedStationIds: Set<string> = new Set();
  
  constructor() {
    super();
    this.title = 'Select QuickMix Stations';
  }
  
  connectedCallback() {
    super.connectedCallback();
    // Initialize selected stations from current QuickMix state
    this.initializeSelection();
  }
  
  updated(changedProperties: Map<string, any>) {
    if (changedProperties.has('stations') || changedProperties.has('open')) {
      this.initializeSelection();
    }
  }
  
  initializeSelection() {
    this.selectedStationIds = new Set(
      this.stations
        .filter(s => s.isQuickMixed && !s.isQuickMix)
        .map(s => s.id)
    );
  }
  
  handleCheckboxChange(stationId: string, checked: boolean) {
    if (checked) {
      this.selectedStationIds.add(stationId);
    } else {
      this.selectedStationIds.delete(stationId);
    }
    this.requestUpdate();
  }
  
  handleSave() {
    const stationIds = Array.from(this.selectedStationIds);
    this.dispatchEvent(new CustomEvent('save', {
      detail: { stationIds }
    }));
  }
  
  static styles = [...ModalBase.styles];
  
  render() {
    // Filter out the QuickMix station itself
    const selectableStations = this.stations.filter(s => !s.isQuickMix);
    
    const body = html`
      <div class="list-container">
        ${selectableStations.map(station => html`
          <div class="list-item">
            <label>
              <input
                type="checkbox"
                .checked=${this.selectedStationIds.has(station.id)}
                @change=${(e: Event) => 
                  this.handleCheckboxChange(station.id, (e.target as HTMLInputElement).checked)
                }
              />
              <span class="item-name">${station.name}</span>
            </label>
          </div>
        `)}
      </div>
    `;
    
    const footer = this.renderStandardFooter('Save', false, false, () => this.handleSave());
    
    return this.renderModal(body, footer);
  }
}

