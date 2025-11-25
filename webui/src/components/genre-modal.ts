import { html, css } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

interface Genre {
  name: string;
  musicId: string;
}

interface GenreCategory {
  name: string;
  genres: Genre[];
}

@customElement('genre-modal')
export class GenreModal extends ModalBase {
  @property({ type: Boolean }) loading = false;
  @property({ type: Array }) categories: GenreCategory[] = [];
  @state() private expandedCategories: Set<string> = new Set();
  @state() private selectedMusicId: string | null = null;
  
  constructor() {
    super();
    this.title = 'Add Genre Station';
  }
  
  toggleCategory(categoryName: string) {
    if (this.expandedCategories.has(categoryName)) {
      this.expandedCategories.delete(categoryName);
    } else {
      this.expandedCategories.add(categoryName);
    }
    this.requestUpdate();
  }
  
  handleGenreSelect(musicId: string) {
    this.selectedMusicId = musicId;
  }
  
  handleCreate() {
    if (this.selectedMusicId) {
      this.dispatchEvent(new CustomEvent('create', {
        detail: { musicId: this.selectedMusicId }
      }));
      this.selectedMusicId = null;
      this.expandedCategories.clear();
    }
  }
  
  protected onCancel() {
    this.selectedMusicId = null;
    this.expandedCategories.clear();
  }
  
  static styles = [
    ...ModalBase.styles,
    css`
    .category-list {
      display: flex;
      flex-direction: column;
      gap: 8px;
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
    
    .genre-list {
      margin-left: 32px;
      margin-top: 8px;
      margin-bottom: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
  `
  ];
  
  render() {
    const body = this.loading 
      ? this.renderLoading('Fetching genres...')
      : html`
        <div class="category-list">
          ${this.categories.map(category => html`
            <div>
              <div class="category-header" @click=${() => this.toggleCategory(category.name)}>
                <span class="material-icons chevron ${this.expandedCategories.has(category.name) ? 'expanded' : ''}">
                  chevron_right
                </span>
                <span class="category-name">${category.name}</span>
              </div>
              ${this.expandedCategories.has(category.name) ? html`
                <div class="genre-list">
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
                        <span class="item-name">${genre.name}</span>
                      </label>
                    </div>
                  `)}
                </div>
              ` : ''}
            </div>
          `)}
        </div>
      `;
    
    const footer = this.renderStandardFooter(
      'Create Station',
      !this.selectedMusicId || this.loading,
      false,
      () => this.handleCreate()
    );
    
    return this.renderModal(body, footer);
  }
}

