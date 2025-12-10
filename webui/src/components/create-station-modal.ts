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

interface Genre {
  name: string;
  musicId: string;
}

interface GenreCategory {
  name: string;
  genres: Genre[];
}

@customElement('create-station-modal')
export class CreateStationModal extends ModalBase {
  @property({ type: String }) currentSongName = '';
  @property({ type: String }) currentArtistName = '';
  @property({ type: String }) currentTrackToken = '';
  @property({ type: Boolean }) loading = false;
  @property({ type: Object }) searchResults: SearchResults = { categories: [] };
  @property({ type: Array }) genreCategories: GenreCategory[] = [];
  @property({ type: Boolean }) genreLoading = false;
  
  @state() private mode: 'select' | 'search-results' | 'browse-genres' = 'select';
  @state() private searchQuery: string = '';
  @state() private expandedCategories: Set<string> = new Set();
  @state() private selectedMusicId: string | null = null;
  @state() private selectedSource: 'song' | 'artist' | 'search' | 'genre' | null = null;
  @state() private sharedStationId: string = '';
  
  constructor() {
    super();
    this.title = 'Create Station';
  }
  
  handleSelectArtist() {
    this.selectedSource = 'artist';
    this.dispatchEvent(new CustomEvent('select-artist'));
  }
  
  handleSelectSong() {
    this.selectedSource = 'song';
    this.dispatchEvent(new CustomEvent('select-song'));
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
    
    this.mode = 'search-results';
    this.selectedMusicId = null;
    this.expandedCategories.clear();
    
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
    this.selectedSource = 'search';
  }
  
  handleCreate() {
    if (this.selectedMusicId && this.selectedSource === 'search') {
      this.dispatchEvent(new CustomEvent('create', {
        detail: { musicId: this.selectedMusicId }
      }));
    }
  }
  
  handleSharedStationInput(e: Event) {
    const input = e.target as HTMLInputElement;
    // Only allow digits
    const value = input.value.replace(/[^0-9]/g, '');
    this.sharedStationId = value;
    input.value = value;
  }
  
  handleSharedStationKeyDown(e: KeyboardEvent) {
    if (e.key === 'Enter' && this.sharedStationId.trim()) {
      this.handleAddSharedStation();
    }
  }
  
  handleAddSharedStation() {
    if (!this.sharedStationId.trim()) {
      return;
    }
    
    this.dispatchEvent(new CustomEvent('shared-station-submit', {
      detail: { stationId: this.sharedStationId.trim() }
    }));
  }
  
  handleBrowseGenres() {
    this.mode = 'browse-genres';
    this.selectedMusicId = null;
    this.expandedCategories.clear();
    
    this.dispatchEvent(new CustomEvent('browse-genres'));
  }
  
  handleGenreSelect(musicId: string) {
    this.selectedMusicId = musicId;
    this.selectedSource = 'genre';
  }
  
  handleGenreCreate() {
    if (!this.selectedMusicId) {
      return;
    }
    
    this.dispatchEvent(new CustomEvent('genre-create', {
      detail: { musicId: this.selectedMusicId }
    }));
  }
  
  handleBackToSelect() {
    this.mode = 'select';
    this.selectedMusicId = null;
    this.selectedSource = null;
    this.expandedCategories.clear();
  }
  
  protected onCancel() {
    this.mode = 'select';
    this.selectedMusicId = null;
    this.selectedSource = null;
    this.expandedCategories.clear();
    this.searchQuery = '';
    this.sharedStationId = '';
  }
  
  static styles = [
    ...ModalBase.styles,
    css`
    .modal {
      max-width: 500px;
      transition: all 0.3s ease;
    }
    
    .modal-body {
      padding: 16px 24px;
    }
    
    /* Select mode styles */
    .select-options {
      display: flex;
      flex-direction: column;
      gap: 12px;
      margin-bottom: 24px;
    }
    
    .option-button {
      display: flex;
      flex-direction: column;
      align-items: flex-start;
      gap: 4px;
      padding: 16px;
      border: none;
      border-radius: 8px;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      transition: all 0.2s;
      text-align: left;
      font-size: 16px;
    }
    
    .option-button:hover {
      background: var(--surface-container-highest);
      transform: translateY(-1px);
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
    }
    
    .option-main {
      display: flex;
      align-items: center;
      gap: 12px;
      font-weight: 500;
    }
    
    .option-detail {
      font-size: 14px;
      color: var(--on-surface-variant);
      margin-left: 36px;
      font-weight: normal;
    }
    
    .divider {
      height: 1px;
      background: var(--outline);
      margin: 8px 0;
    }
    
    .search-section {
      margin-top: 16px;
    }
    
    .search-section .button-confirm {
      width: 90px;
    }
    
    .search-label {
      font-size: 17px;
      font-weight: 500;
      color: var(--on-surface);
      margin-bottom: 12px;
      margin-top: 8px;
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
    
    
    /* Search results styles */
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
  `
  ];
  
  renderSelectMode() {
    const body = html`
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
        
        <div class="search-input-container" style="margin-top: 12px;">
          <input
            type="text"
            class="search-input"
            placeholder="Enter shared station ID (digits only)"
            .value=${this.sharedStationId}
            @input=${this.handleSharedStationInput}
            @keydown=${this.handleSharedStationKeyDown}
          >
          <button 
            class="button-confirm" 
            @click=${this.handleAddSharedStation}
            ?disabled=${!this.sharedStationId.trim()}
          >
            Add
          </button>
        </div>
      </div>
      
      <div class="divider"></div>
      
      <div class="search-label">Or create from:</div>
      
      <div class="select-options">
        <button class="option-button" @click=${this.handleSelectArtist} ?disabled=${!this.currentTrackToken}>
          <div class="option-main">
            <span class="material-icons">person</span>
            <span>The current Artist</span>
          </div>
          ${this.currentArtistName ? html`
            <div class="option-detail">${this.currentArtistName}</div>
          ` : ''}
        </button>
        
        <button class="option-button" @click=${this.handleSelectSong} ?disabled=${!this.currentTrackToken}>
          <div class="option-main">
            <span class="material-icons">music_note</span>
            <span>The current Song</span>
          </div>
          ${this.currentSongName ? html`
            <div class="option-detail">${this.currentSongName}</div>
          ` : ''}
        </button>
        
        <button class="option-button" @click=${this.handleBrowseGenres}>
          <div class="option-main">
            <span class="material-icons">library_music</span>
            <span>Select Genre</span>
          </div>
        </button>
      </div>
    `;
    
    return this.renderModal(body);
  }
  
  renderSearchResultsMode() {
    const searchSection = html`
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
    `;
    
    let resultsContent;
    
    if (this.loading) {
      resultsContent = this.renderLoading('Searching...');
    } else if (!this.searchResults?.categories || this.searchResults.categories.length === 0) {
      resultsContent = html`<div class="no-results">No results found for "${this.searchQuery}"</div>`;
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
    
    const footer = this.renderStandardFooter(
      'Create Station',
      !this.selectedMusicId || this.loading,
      false,
      () => this.handleCreate()
    );
    
    return this.renderModal(body, footer);
  }
  
  renderBrowseGenresMode() {
    let content;
    
    if (this.genreLoading) {
      content = this.renderLoading('Loading genres...');
    } else if (!this.genreCategories || this.genreCategories.length === 0) {
      content = html`<div class="no-results">No genres available</div>`;
    } else {
      content = html`
        <div class="category-list">
          ${this.genreCategories.map(category => html`
            <div>
              <div class="category-header" @click=${() => this.toggleCategory(category.name)}>
                <span class="material-icons chevron ${this.expandedCategories.has(category.name) ? 'expanded' : ''}">
                  chevron_right
                </span>
                <span class="category-name">${category.name}</span>
                <span class="category-count">(${category.genres.length})</span>
              </div>
              ${this.expandedCategories.has(category.name) ? html`
                <div class="results-list">
                  ${category.genres.map(genre => html`
                    <div class="list-item">
                      <label>
                        <input
                          type="radio"
                          name="genre-select"
                          .value=${genre.musicId}
                          .checked=${this.selectedMusicId === genre.musicId}
                          @change=${() => this.handleGenreSelect(genre.musicId)}
                        >
                        <span class="result-item-name">${genre.name}</span>
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
    
    const body = html`${content}`;
    
    const footer = html`
      <div class="modal-footer">
        <button class="button-cancel" @click=${this.handleBackToSelect}>
          Back
        </button>
        <button 
          class="button-confirm"
          ?disabled=${!this.selectedMusicId || this.genreLoading}
          @click=${this.handleGenreCreate}
        >
          Create Station
        </button>
      </div>
    `;
    
    return this.renderModal(body, footer);
  }
  
  render() {
    if (this.mode === 'select') {
      return this.renderSelectMode();
    } else if (this.mode === 'browse-genres') {
      return this.renderBrowseGenresMode();
    } else {
      return this.renderSearchResultsMode();
    }
  }
}
