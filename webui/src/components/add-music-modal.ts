import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface SearchResult {
  name?: string;
  title?: string;
  artist?: string;
  musicId: string;
}

interface SearchCategory {
  name: string;
  results: SearchResult[];
}

interface SearchResults {
  categories: SearchCategory[];
}

interface Station {
  id: string;
  name: string;
  isQuickMix?: boolean;
}

@customElement('add-music-modal')
export class AddMusicModal extends ModalBase {
  @property({ type: Array }) stations: Station[] = [];
  @property({ type: Boolean }) loading = false;
  @property({ type: Object }) searchResults: SearchResults = { categories: [] };
  
  @state() private stage: 'select-station' | 'search' = 'select-station';
  @state() private selectedStationId: string | null = null;
  @state() private selectedStationName: string = '';
  @state() private searchQuery: string = '';
  @state() private expandedCategories: Set<string> = new Set();
  @state() private selectedMusicId: string | null = null;
  @state() private searchPerformed: boolean = false;
  
  constructor() {
    super();
    this.title = 'Add Music to Station';
  }
  
  handleStationSelect(stationId: string, stationName: string) {
    this.selectedStationId = stationId;
    this.selectedStationName = stationName;
  }
  
  handleNext() {
    if (this.selectedStationId) {
      this.stage = 'search';
      this.title = `Add Music to ${this.selectedStationName}`;
    }
  }
  
  handleBack() {
    this.stage = 'select-station';
    this.title = 'Add Music to Station';
    this.searchQuery = '';
    this.selectedMusicId = null;
    this.expandedCategories.clear();
    this.searchPerformed = false;
  }
  
  handleSearchInput(e: Event) {
    const input = e.target as HTMLInputElement;
    this.searchQuery = input.value;
  }
  
  handleSearchKeyDown(e: KeyboardEvent) {
    if (e.key === 'Enter' && this.searchQuery.trim()) {
      this.handleSearch();
    }
  }
  
  handleSearch() {
    if (!this.searchQuery.trim()) {
      return;
    }
    
    this.selectedMusicId = null;
    this.expandedCategories.clear();
    this.searchPerformed = true;
    
    this.dispatchEvent(new CustomEvent('search', {
      detail: { query: this.searchQuery.trim() }
    }));
  }
  
  toggleCategory(categoryName: string) {
    if (this.expandedCategories.has(categoryName)) {
      this.expandedCategories.delete(categoryName);
    } else {
      this.expandedCategories.add(categoryName);
    }
    this.requestUpdate();
  }
  
  handleResultSelect(musicId: string) {
    this.selectedMusicId = musicId;
  }
  
  handleAddMusic() {
    if (this.selectedMusicId && this.selectedStationId) {
      this.dispatchEvent(new CustomEvent('add-music', {
        detail: { 
          musicId: this.selectedMusicId,
          stationId: this.selectedStationId
        }
      }));
    }
  }
  
  protected onCancel() {
    this.stage = 'select-station';
    this.title = 'Add Music to Station';
    this.selectedStationId = null;
    this.selectedStationName = '';
    this.selectedMusicId = null;
    this.expandedCategories.clear();
    this.searchQuery = '';
    this.searchPerformed = false;
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
    
    .search-section {
      margin-bottom: 16px;
    }
    
    .search-input-container {
      display: flex;
      gap: 12px;
    }
    
    .search-input {
      flex: 1;
      padding: 12px 16px;
      border: 1px solid var(--outline);
      border-radius: 8px;
      background: var(--surface-variant);
      color: var(--on-surface);
      font-size: 16px;
      font-family: inherit;
    }
    
    .search-input:focus {
      outline: none;
      border-color: var(--primary);
      background: var(--surface);
    }
    
    .category-list {
      display: flex;
      flex-direction: column;
      gap: 8px;
      margin-top: 16px;
    }
    
    .category-header {
      display: flex;
      align-items: center;
      padding: 12px;
      cursor: pointer;
      background: var(--surface-variant);
      border-radius: 8px;
      transition: background 0.2s;
      user-select: none;
    }
    
    .category-header:hover {
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
    
    .category-name {
      font-size: 16px;
      font-weight: 500;
      color: var(--on-surface);
    }
    
    .category-count {
      margin-left: auto;
      font-size: 14px;
      color: var(--on-surface-variant);
    }
    
    .results-list {
      margin-left: 32px;
      margin-top: 8px;
      margin-bottom: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    
    .result-item-name {
      font-size: 14px;
      color: var(--on-surface);
    }
    
    .result-item-artist {
      font-size: 12px;
      color: var(--on-surface-variant);
      margin-left: 4px;
    }
    
    .no-results {
      text-align: center;
      padding: 40px;
      color: var(--on-surface-variant);
      font-size: 16px;
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
  
  renderSearch() {
    const searchSection = html`
      <div class="search-section">
        <div class="search-input-container">
          <input
            type="text"
            class="search-input"
            placeholder="Search for artist or song..."
            .value=${this.searchQuery}
            @input=${this.handleSearchInput}
            @keydown=${this.handleSearchKeyDown}
          >
          <button 
            class="button-confirm" 
            @click=${this.handleSearch}
            ?disabled=${!this.searchQuery.trim() || this.loading}
          >
            Search
          </button>
        </div>
      </div>
    `;
    
    let resultsContent;
    
    if (this.loading) {
      resultsContent = this.renderLoading('Searching...');
    } else if (!this.searchResults?.categories || this.searchResults.categories.length === 0) {
      if (this.searchPerformed) {
        resultsContent = html`<div class="no-results">No results found</div>`;
      } else {
        resultsContent = html`<div class="no-results">Search for an artist or song to add</div>`;
      }
    } else {
      resultsContent = html`
        <div class="category-list">
          ${this.searchResults.categories.map(category => html`
            <div>
              <div class="category-header" @click=${() => this.toggleCategory(category.name)}>
                <span class="material-icons chevron ${this.expandedCategories.has(category.name) ? 'expanded' : ''}">
                  chevron_right
                </span>
                <span class="category-name">${category.name}</span>
                <span class="category-count">(${category.results.length})</span>
              </div>
              ${this.expandedCategories.has(category.name) ? html`
                <div class="results-list">
                  ${category.results.map(result => html`
                    <div class="list-item">
                      <label>
                        <input
                          type="radio"
                          name="search-result-select"
                          .value=${result.musicId}
                          .checked=${this.selectedMusicId === result.musicId}
                          @change=${() => this.handleResultSelect(result.musicId)}
                        >
                        <span class="result-item-name">
                          ${result.name || result.title}
                          ${result.artist ? html`<span class="result-item-artist">by ${result.artist}</span>` : ''}
                        </span>
                      </label>
                    </div>
                  `)}
                </div>
              ` : ''}
            </div>
          `)}
        </div>
      `;
    }
    
    const body = html`
      ${searchSection}
      ${resultsContent}
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
          ?disabled=${!this.selectedMusicId || this.loading}
          @click=${this.handleAddMusic}
        >
          Add Music
        </button>
      </div>
    `;
    
    return this.renderModal(body, footer);
  }
  
  render() {
    if (this.stage === 'select-station') {
      return this.renderSelectStation();
    } else {
      return this.renderSearch();
    }
  }
}

