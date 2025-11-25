import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Station {
  id: string;
  name: string;
  isQuickMix?: boolean;
}

@customElement('rename-station-modal')
export class RenameStationModal extends ModalBase {
  @property({ type: Array }) stations: Station[] = [];
  
  @state() private stage: 'select-station' | 'enter-name' = 'select-station';
  @state() private selectedStationId: string | null = null;
  @state() private selectedStationName: string = '';
  @state() private newName: string = '';
  
  constructor() {
    super();
    this.title = 'Rename Station';
  }
  
  handleStationSelect(stationId: string, stationName: string) {
    this.selectedStationId = stationId;
    this.selectedStationName = stationName;
  }
  
  handleNext() {
    if (this.selectedStationId) {
      this.stage = 'enter-name';
      this.newName = this.selectedStationName; // Pre-fill with current name
      this.title = `Rename: ${this.selectedStationName}`;
    }
  }
  
  handleBack() {
    this.stage = 'select-station';
    this.title = 'Rename Station';
    this.newName = '';
  }
  
  handleNameInput(e: Event) {
    const input = e.target as HTMLInputElement;
    this.newName = input.value;
  }
  
  handleRename() {
    if (this.selectedStationId && this.newName.trim() && this.newName !== this.selectedStationName) {
      this.dispatchEvent(new CustomEvent('rename-station', {
        detail: { 
          stationId: this.selectedStationId,
          newName: this.newName.trim()
        }
      }));
    }
  }
  
  protected onCancel() {
    this.stage = 'select-station';
    this.title = 'Rename Station';
    this.selectedStationId = null;
    this.selectedStationName = '';
    this.newName = '';
  }
  
  static styles = [
    ModalBase.styles,
    css`
    .modal {
      max-width: 500px;
    }
    
    .station-list {
      display: flex;
      flex-direction: column;
      gap: 8px;
      max-height: 400px;
      overflow-y: auto;
    }
    
    .station-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: var(--surface-variant);
      border-radius: 8px;
      cursor: pointer;
      transition: background 0.2s;
      user-select: none;
    }
    
    .station-item:hover {
      background: var(--surface-container-high);
    }
    
    .station-item label {
      display: flex;
      align-items: center;
      gap: 12px;
      cursor: pointer;
      flex: 1;
    }
    
    .station-name {
      font-size: 14px;
      color: var(--on-surface);
    }
    
    .name-input-section {
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    
    .current-station {
      font-size: 14px;
      color: var(--on-surface-variant);
      margin-bottom: 8px;
    }
    
    .name-input {
      width: 100%;
      padding: 12px 16px;
      border: 1px solid var(--outline);
      border-radius: 8px;
      background: var(--surface-variant);
      color: var(--on-surface);
      font-size: 16px;
      font-family: inherit;
      box-sizing: border-box;
    }
    
    .name-input:focus {
      outline: none;
      border-color: var(--primary);
      background: var(--surface);
    }
    
    .info-note {
      padding: 12px;
      background: var(--surface-container-highest);
      border-radius: 8px;
      font-size: 13px;
      color: var(--on-surface-variant);
      line-height: 1.4;
    }
    
    .back-button {
      margin-right: auto;
    }
  `
  ];
  
  renderSelectStation() {
    // Filter out QuickMix stations
    const selectableStations = this.stations.filter(s => !s.isQuickMix);
    
    const body = html`
      <div class="station-list">
        ${selectableStations.map(station => html`
          <div class="station-item">
            <label>
              <input
                type="radio"
                name="station-select"
                .value=${station.id}
                .checked=${this.selectedStationId === station.id}
                @change=${() => this.handleStationSelect(station.id, station.name)}
              >
              <span class="station-name">${station.name}</span>
            </label>
          </div>
        `)}
      </div>
    `;
    
    const footer = this.renderStandardFooter(
      'Next',
      !this.selectedStationId,
      false,
      () => this.handleNext()
    );
    
    return this.renderModal(body, footer);
  }
  
  renderEnterName() {
    const isChanged = this.newName.trim() && this.newName !== this.selectedStationName;
    
    const body = html`
      <div class="name-input-section">
        <div class="current-station">
          Current name: <strong>${this.selectedStationName}</strong>
        </div>
        
        <input
          type="text"
          class="name-input"
          placeholder="Enter new station name"
          .value=${this.newName}
          @input=${this.handleNameInput}
          @keydown=${(e: KeyboardEvent) => {
            if (e.key === 'Enter' && isChanged) {
              this.handleRename();
            }
          }}
          autofocus
        >
        
        <div class="info-note">
          Note: Pandora may not allow some stations to be renamed.
        </div>
      </div>
    `;
    
    const footer = html`
      <div class="modal-footer">
        <button class="button-cancel back-button" @click=${this.handleBack}>
          Back
        </button>
        <button class="button-cancel" @click=${this.handleCancel}>
          Cancel
        </button>
        <button 
          class="button-confirm" 
          ?disabled=${!isChanged}
          @click=${this.handleRename}
        >
          Rename
        </button>
      </div>
    `;
    
    return this.renderModal(body, footer);
  }
  
  render() {
    if (this.stage === 'select-station') {
      return this.renderSelectStation();
    } else {
      return this.renderEnterName();
    }
  }
}

