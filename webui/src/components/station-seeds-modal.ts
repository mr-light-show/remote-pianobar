import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Station {
  id: string;
  name: string;
}

interface ArtistSeed {
  seedId: string;
  name: string;
}

interface SongSeed {
  seedId: string;
  title: string;
  artist: string;
}

interface StationSeed {
  seedId: string;
  name: string;
}

interface Feedback {
  feedbackId: string;
  title: string;
  artist: string;
  rating: number; // 1 = love, 0 = ban
}

interface StationInfo {
  artistSeeds: ArtistSeed[];
  songSeeds: SongSeed[];
  stationSeeds: StationSeed[];
  feedback: Feedback[];
}

@customElement('station-seeds-modal')
export class StationSeedsModal extends ModalBase {
  @property({ type: Array }) stations: Station[] = [];
  @property({ type: Object }) stationInfo: StationInfo | null = null;
  @property({ type: Boolean }) infoLoading = false;
  
  @state() private stage: 'select-station' | 'manage-seeds' = 'select-station';
  @state() private selectedStationId: string | null = null;
  @state() private selectedStationName: string = '';
  @state() private expandedSections: Set<string> = new Set(['artistSeeds', 'songSeeds', 'stationSeeds', 'feedback']);
  
  constructor() {
    super();
    this.title = 'Manage Seeds & Feedback';
  }
  
  handleStationSelect(stationId: string, stationName: string) {
    this.selectedStationId = stationId;
    this.selectedStationName = stationName;
  }
  
  handleNext() {
    if (this.selectedStationId) {
      this.stage = 'manage-seeds';
      this.title = `Seeds: ${this.selectedStationName}`;
      // Emit event to request station info
      this.dispatchEvent(new CustomEvent('get-info', {
        detail: { stationId: this.selectedStationId }
      }));
    }
  }
  
  handleBack() {
    this.stage = 'select-station';
    this.title = 'Manage Seeds & Feedback';
  }
  
  toggleSection(section: string) {
    if (this.expandedSections.has(section)) {
      this.expandedSections.delete(section);
    } else {
      this.expandedSections.add(section);
    }
    this.requestUpdate();
  }
  
  handleDeleteSeed(seedId: string, seedType: string) {
    if (this.selectedStationId) {
      this.dispatchEvent(new CustomEvent('delete-seed', {
        detail: { 
          seedId,
          seedType,
          stationId: this.selectedStationId
        }
      }));
    }
  }
  
  handleDeleteFeedback(feedbackId: string) {
    if (this.selectedStationId) {
      this.dispatchEvent(new CustomEvent('delete-feedback', {
        detail: { 
          feedbackId,
          stationId: this.selectedStationId
        }
      }));
    }
  }
  
  protected onCancel() {
    this.stage = 'select-station';
    this.title = 'Manage Seeds & Feedback';
    this.selectedStationId = null;
    this.selectedStationName = '';
    this.expandedSections = new Set(['artistSeeds', 'songSeeds', 'stationSeeds', 'feedback']);
  }
  
  static override styles = [
    ModalBase.styles,
    css`
    .modal {
      max-width: 700px;
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
    
    .seeds-container {
      display: flex;
      flex-direction: column;
      gap: 12px;
      max-height: 600px;
      overflow-y: auto;
    }
    
    .section {
      border: 1px solid var(--outline);
      border-radius: 8px;
      overflow: hidden;
    }
    
    .section-header {
      display: flex;
      align-items: center;
      padding: 12px 16px;
      cursor: pointer;
      background: var(--surface-variant);
      transition: background 0.2s;
      user-select: none;
    }
    
    .section-header:hover {
      background: var(--surface-container-high);
    }
    
    .chevron {
      margin-right: 8px;
      transition: transform 0.2s;
      font-size: 20px;
      color: var(--on-surface-variant);
    }
    
    .chevron.expanded {
      transform: rotate(90deg);
    }
    
    .section-title {
      font-size: 16px;
      font-weight: 500;
      color: var(--on-surface);
      flex: 1;
    }
    
    .section-count {
      font-size: 14px;
      color: var(--on-surface-variant);
    }
    
    .section-content {
      padding: 8px;
    }
    
    .seed-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: var(--surface);
      border-radius: 8px;
      margin-bottom: 8px;
    }
    
    .seed-info {
      flex: 1;
      min-width: 0;
    }
    
    .seed-name {
      font-size: 14px;
      color: var(--on-surface);
      font-weight: 500;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    
    .seed-artist {
      font-size: 12px;
      color: var(--on-surface-variant);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    
    .delete-button {
      padding: 8px 16px;
      border: none;
      border-radius: 6px;
      background: var(--error-container);
      color: var(--on-error-container);
      cursor: pointer;
      font-size: 13px;
      font-weight: 500;
      transition: all 0.2s;
    }
    
    .delete-button:hover {
      background: var(--error);
      color: var(--on-error);
      transform: translateY(-1px);
    }
    
    .feedback-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: var(--surface);
      border-radius: 8px;
      margin-bottom: 8px;
    }
    
    .feedback-icon {
      font-size: 24px;
    }
    
    .feedback-icon.loved {
      color: #4CAF50;
    }
    
    .feedback-icon.banned {
      color: #F44336;
    }
    
    .no-items {
      padding: 24px;
      text-align: center;
      color: var(--on-surface-variant);
      font-size: 14px;
    }
    
    .back-button {
      margin-right: auto;
    }
  `
  ];
  
  renderSelectStation() {
    const body = html`
      <div class="station-list">
        ${this.stations.map(station => html`
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
  
  renderManageSeeds() {
    const hasAnyItems = this.stationInfo && (
      this.stationInfo.artistSeeds.length > 0 ||
      this.stationInfo.songSeeds.length > 0 ||
      this.stationInfo.stationSeeds.length > 0 ||
      this.stationInfo.feedback.length > 0
    );
    
    const body = html`
      ${this.infoLoading ? this.renderLoading('Loading station info...') : html`
        ${!hasAnyItems ? html`
          <div class="no-items">
            No seeds or feedback available for this station.
          </div>
        ` : html`
          <div class="seeds-container">
            ${this.stationInfo && this.stationInfo.artistSeeds.length > 0 ? html`
              <div class="section">
                <div class="section-header" @click=${() => this.toggleSection('artistSeeds')}>
                  <span class="material-icons chevron ${this.expandedSections.has('artistSeeds') ? 'expanded' : ''}">
                    chevron_right
                  </span>
                  <span class="section-title">Artist Seeds</span>
                  <span class="section-count">(${this.stationInfo.artistSeeds.length})</span>
                </div>
                ${this.expandedSections.has('artistSeeds') ? html`
                  <div class="section-content">
                    ${this.stationInfo.artistSeeds.map(artist => html`
                      <div class="seed-item">
                        <span class="material-icons">person</span>
                        <div class="seed-info">
                          <div class="seed-name">${artist.name}</div>
                        </div>
                        <button class="delete-button" @click=${() => this.handleDeleteSeed(artist.seedId, 'artist')}>
                          Delete
                        </button>
                      </div>
                    `)}
                  </div>
                ` : ''}
              </div>
            ` : ''}
            
            ${this.stationInfo && this.stationInfo.songSeeds.length > 0 ? html`
              <div class="section">
                <div class="section-header" @click=${() => this.toggleSection('songSeeds')}>
                  <span class="material-icons chevron ${this.expandedSections.has('songSeeds') ? 'expanded' : ''}">
                    chevron_right
                  </span>
                  <span class="section-title">Song Seeds</span>
                  <span class="section-count">(${this.stationInfo.songSeeds.length})</span>
                </div>
                ${this.expandedSections.has('songSeeds') ? html`
                  <div class="section-content">
                    ${this.stationInfo.songSeeds.map(song => html`
                      <div class="seed-item">
                        <span class="material-icons">music_note</span>
                        <div class="seed-info">
                          <div class="seed-name">${song.title}</div>
                          <div class="seed-artist">${song.artist}</div>
                        </div>
                        <button class="delete-button" @click=${() => this.handleDeleteSeed(song.seedId, 'song')}>
                          Delete
                        </button>
                      </div>
                    `)}
                  </div>
                ` : ''}
              </div>
            ` : ''}
            
            ${this.stationInfo && this.stationInfo.stationSeeds.length > 0 ? html`
              <div class="section">
                <div class="section-header" @click=${() => this.toggleSection('stationSeeds')}>
                  <span class="material-icons chevron ${this.expandedSections.has('stationSeeds') ? 'expanded' : ''}">
                    chevron_right
                  </span>
                  <span class="section-title">Station Seeds</span>
                  <span class="section-count">(${this.stationInfo.stationSeeds.length})</span>
                </div>
                ${this.expandedSections.has('stationSeeds') ? html`
                  <div class="section-content">
                    ${this.stationInfo.stationSeeds.map(station => html`
                      <div class="seed-item">
                        <span class="material-icons">radio</span>
                        <div class="seed-info">
                          <div class="seed-name">${station.name}</div>
                        </div>
                        <button class="delete-button" @click=${() => this.handleDeleteSeed(station.seedId, 'station')}>
                          Delete
                        </button>
                      </div>
                    `)}
                  </div>
                ` : ''}
              </div>
            ` : ''}
            
            ${this.stationInfo && this.stationInfo.feedback.length > 0 ? html`
              <div class="section">
                <div class="section-header" @click=${() => this.toggleSection('feedback')}>
                  <span class="material-icons chevron ${this.expandedSections.has('feedback') ? 'expanded' : ''}">
                    chevron_right
                  </span>
                  <span class="section-title">Feedback</span>
                  <span class="section-count">(${this.stationInfo.feedback.length})</span>
                </div>
                ${this.expandedSections.has('feedback') ? html`
                  <div class="section-content">
                    ${this.stationInfo.feedback.map(feedback => html`
                      <div class="feedback-item">
                        <span class="material-icons feedback-icon ${feedback.rating === 1 ? 'loved' : 'banned'}">
                          ${feedback.rating === 1 ? 'thumb_up' : 'thumb_down'}
                        </span>
                        <div class="seed-info">
                          <div class="seed-name">${feedback.title}</div>
                          <div class="seed-artist">${feedback.artist}</div>
                        </div>
                        <button class="delete-button" @click=${() => this.handleDeleteFeedback(feedback.feedbackId)}>
                          Delete
                        </button>
                      </div>
                    `)}
                  </div>
                ` : ''}
              </div>
            ` : ''}
          </div>
        `}
      `}
    `;
    
    const footer = html`
      <div class="modal-footer">
        <button class="button-cancel back-button" @click=${this.handleBack}>
          Back
        </button>
        <button class="button-cancel" @click=${this.handleCancel}>
          Close
        </button>
      </div>
    `;
    
    return this.renderModal(body, footer);
  }
  
  render() {
    if (this.stage === 'select-station') {
      return this.renderSelectStation();
    } else {
      return this.renderManageSeeds();
    }
  }
}

