import { html, css, nothing, type TemplateResult } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface StationMode {
  id: number;
  name: string;
  description: string;
  active: boolean;
}

interface StationRow {
  id: string;
  name: string;
}

@customElement('station-mode-modal')
export class StationModeModal extends ModalBase {
  @property({ type: String }) currentStationId: string = '';
  @property({ type: String }) currentStationName: string = '';
  /** Used to resolve id when currentStationId is empty but name matches a station. */
  @property({ type: Array }) stations: StationRow[] = [];
  @property({ type: Array }) modes: StationMode[] = [];
  @property({ type: Boolean}) modesLoading = false;
  
  @state() private selectedModeId: number | null = null;
  
  constructor() {
    super();
    this.title = 'Manage Station Mode';
  }
  
  updated(changedProperties: Map<string, any>) {
    super.updated(changedProperties);
    // Update title when station name changes
    if (changedProperties.has('currentStationName') && this.currentStationName) {
      this.title = `Station Mode: ${this.currentStationName}`;
    }
  }

  protected override renderModalHeaderExtra(): TemplateResult | typeof nothing {
    const id = this.resolvedStationId();
    if (!id) {
      return nothing;
    }
    return html`<div class="station-id-compact" title="Station ID">${id}</div>`;
  }
  
  handleModeSelect(modeId: number) {
    this.selectedModeId = modeId;
  }
  
  handleSetMode() {
    const stationId = this.resolvedStationId();
    if (stationId && this.selectedModeId !== null) {
      this.dispatchEvent(new CustomEvent('set-mode', {
        detail: { 
          stationId,
          modeId: this.selectedModeId
        }
      }));
    }
  }
  
  protected onCancel() {
    this.selectedModeId = null;
  }

  /** Prefer explicit id; otherwise match currentStationName to stations[]. */
  private resolvedStationId(): string {
    const id = (this.currentStationId || '').trim();
    if (id) {
      return id;
    }
    const name = (this.currentStationName || '').trim();
    if (!name || !Array.isArray(this.stations)) {
      return '';
    }
    const row = this.stations.find((s) => s.name === name);
    return (row?.id || '').trim();
  }
  
  static override styles = [
    ...ModalBase.styles,
    css`
    .modal {
      max-width: 600px;
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

    .station-id-compact {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-size: 10px;
      color: var(--on-surface-variant);
      line-height: 1.4;
      margin-top: 8px;
      word-break: break-all;
    }
  `
  ];
  
  render() {
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
    
    const footer = this.renderStandardFooter(
      'Set Mode',
      this.selectedModeId === null || this.modesLoading,
      false,
      () => this.handleSetMode()
    );
    
    return this.renderModal(body, footer);
  }
}

