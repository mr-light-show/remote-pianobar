import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Station {
  id: string;
  name: string;
  isQuickMix?: boolean;
}

interface StationMode {
  id: number;
  name: string;
  description: string;
  active: boolean;
}

@customElement('station-mode-modal')
export class StationModeModal extends ModalBase {
  @property({ type: Array }) stations: Station[] = [];
  @property({ type: Array }) modes: StationMode[] = [];
  @property({ type: Boolean}) modesLoading = false;
  
  @state() private stage: 'select-station' | 'select-mode' = 'select-station';
  @state() private selectedStationId: string | null = null;
  @state() private selectedStationName: string = '';
  @state() private selectedModeId: number | null = null;
  
  constructor() {
    super();
    this.title = 'Manage Station Mode';
  }
  
  handleStationSelect(stationId: string, stationName: string) {
    this.selectedStationId = stationId;
    this.selectedStationName = stationName;
  }
  
  handleNext() {
    if (this.selectedStationId) {
      this.stage = 'select-mode';
      this.title = `Station Mode: ${this.selectedStationName}`;
      // Emit event to request modes
      this.dispatchEvent(new CustomEvent('get-modes', {
        detail: { stationId: this.selectedStationId }
      }));
    }
  }
  
  handleBack() {
    this.stage = 'select-station';
    this.title = 'Manage Station Mode';
    this.selectedModeId = null;
  }
  
  handleModeSelect(modeId: number) {
    this.selectedModeId = modeId;
  }
  
  handleSetMode() {
    if (this.selectedStationId !== null && this.selectedModeId !== null) {
      this.dispatchEvent(new CustomEvent('set-mode', {
        detail: { 
          stationId: this.selectedStationId,
          modeId: this.selectedModeId
        }
      }));
    }
  }
  
  protected onCancel() {
    this.stage = 'select-station';
    this.title = 'Manage Station Mode';
    this.selectedStationId = null;
    this.selectedStationName = '';
    this.selectedModeId = null;
  }
  
  static override styles = [
    ModalBase.styles,
    css`
    .modal {
      max-width: 600px;
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
    
    .modes-list {
      display: flex;
      flex-direction: column;
      gap: 12px;
      max-height: 500px;
      overflow-y: auto;
    }
    
    .mode-item {
      display: flex;
      flex-direction: column;
      gap: 8px;
      padding: 16px;
      background: var(--surface-variant);
      border-radius: 8px;
      cursor: pointer;
      transition: all 0.2s;
      user-select: none;
      border: 2px solid transparent;
    }
    
    .mode-item:hover {
      background: var(--surface-container-high);
    }
    
    .mode-item.active {
      border-color: var(--primary);
      background: var(--primary-container);
    }
    
    .mode-item.selected {
      border-color: var(--primary);
    }
    
    .mode-header {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    
    .mode-name {
      font-size: 16px;
      font-weight: 500;
      color: var(--on-surface);
      flex: 1;
    }
    
    .mode-active-badge {
      padding: 4px 12px;
      background: var(--primary);
      color: var(--on-primary);
      border-radius: 12px;
      font-size: 12px;
      font-weight: 600;
    }
    
    .mode-description {
      font-size: 14px;
      color: var(--on-surface-variant);
      line-height: 1.4;
    }
    
    .info-note {
      padding: 12px;
      background: var(--surface-container-highest);
      border-radius: 8px;
      font-size: 13px;
      color: var(--on-surface-variant);
      line-height: 1.4;
      margin-bottom: 16px;
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
  
  renderSelectMode() {
    const body = html`
      <div class="info-note">
        Note: Changing the station mode will restart playback.
      </div>
      
      ${this.modesLoading ? this.renderLoading('Loading modes...') : html`
        <div class="modes-list">
          ${this.modes.map(mode => html`
            <div 
              class="mode-item ${mode.active ? 'active' : ''} ${this.selectedModeId === mode.id ? 'selected' : ''}"
              @click=${() => this.handleModeSelect(mode.id)}
            >
              <div class="mode-header">
                <input
                  type="radio"
                  name="mode-select"
                  .value=${mode.id}
                  .checked=${this.selectedModeId === mode.id}
                  @change=${() => this.handleModeSelect(mode.id)}
                >
                <span class="mode-name">${mode.name}</span>
                ${mode.active ? html`<span class="mode-active-badge">ACTIVE</span>` : ''}
              </div>
              <div class="mode-description">${mode.description}</div>
            </div>
          `)}
        </div>
      `}
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
          ?disabled=${this.selectedModeId === null || this.modesLoading}
          @click=${this.handleSetMode}
        >
          Set Mode
        </button>
      </div>
    `;
    
    return this.renderModal(body, footer);
  }
  
  render() {
    if (this.stage === 'select-station') {
      return this.renderSelectStation();
    } else {
      return this.renderSelectMode();
    }
  }
}

