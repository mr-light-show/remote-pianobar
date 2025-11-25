import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

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
  @property({ type: String }) currentStationId: string = '';
  @property({ type: String }) currentStationName: string = '';
  @property({ type: Object }) stationInfo: StationInfo | null = null;
  @property({ type: Boolean }) infoLoading = false;
  
  @state() private expandedSections: Set<string> = new Set(['artistSeeds', 'songSeeds', 'stationSeeds', 'feedback']);
  
  constructor() {
    super();
    this.title = 'Manage Seeds & Feedback';
  }
  
  updated(changedProperties: Map<string, any>) {
    super.updated(changedProperties);
    // Update title when station name changes
    if (changedProperties.has('currentStationName') && this.currentStationName) {
      this.title = `Seeds: ${this.currentStationName}`;
    }
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
    if (this.currentStationId) {
      this.dispatchEvent(new CustomEvent('delete-seed', {
        detail: { 
          seedId,
          seedType,
          stationId: this.currentStationId
        }
      }));
    }
  }
  
  handleDeleteFeedback(feedbackId: string) {
    if (this.currentStationId) {
      this.dispatchEvent(new CustomEvent('delete-feedback', {
        detail: { 
          feedbackId,
          stationId: this.currentStationId
        }
      }));
    }
  }
  
  protected onCancel() {
    this.expandedSections = new Set(['artistSeeds', 'songSeeds', 'stationSeeds', 'feedback']);
  }
  
  static override styles = [
    ...ModalBase.styles,
    css`
    .modal {
      max-width: 700px;
    }
    
    .seeds-container {
      display: flex;
      flex-direction: column;
      gap: 8px;
      max-height: 600px;
      overflow-y: auto;
    }
    
    .section-header {
      display: flex;
      align-items: center;
      padding: 12px;
      cursor: pointer;
      background: var(--surface-variant);
      border-radius: 8px;
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
      margin-left: 32px;
      margin-top: 8px;
      margin-bottom: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    
    .seed-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: var(--surface-variant);
      border-radius: 8px;
      margin-bottom: 4px;
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
      background: var(--surface-variant);
      border-radius: 8px;
      margin-bottom: 4px;
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
  `
  ];
  
  render() {
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
    
    const footer = this.renderStandardFooter(
      'Close',
      false,
      false,
      () => this.handleCancel()
    );
    
    return this.renderModal(body, footer);
  }
}

