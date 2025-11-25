import { LitElement, html, css } from 'lit';
import { customElement, state } from 'lit/decorators.js';

@customElement('info-menu')
export class InfoMenu extends LitElement {
  @state() private menuOpen = false;
  
  connectedCallback() {
    super.connectedCallback();
    // Listen for clicks on document to close menu
    document.addEventListener('click', this.handleClickOutside);
  }
  
  disconnectedCallback() {
    super.disconnectedCallback();
    // Clean up listener
    document.removeEventListener('click', this.handleClickOutside);
  }
  
  private handleClickOutside = (event: MouseEvent) => {
    // Check if click is outside this component
    if (this.menuOpen && !event.composedPath().includes(this)) {
      this.menuOpen = false;
    }
  };
  
  static styles = css`
    :host {
      position: absolute;
      top: 0;
      right: 0;
    }
    
    .menu-popup {
      position: absolute;
      top: 56px;
      right: 0;
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
      z-index: 100;
      min-width: 220px;
    }
    
    .menu-popup.hidden {
      display: none;
    }
    
    .action-button {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px 16px;
      border: none;
      border-radius: 8px;
      background: transparent;
      color: var(--on-surface);
      cursor: pointer;
      transition: all 0.2s;
      text-align: left;
      font-size: 14px;
    }
    
    .action-button:hover {
      background: var(--surface-variant);
    }
    
    .action-button.delete {
      color: var(--error);
    }
    
    .action-button.delete:hover {
      background: rgba(255, 0, 0, 0.1);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;
  
  toggleMenu() {
    // Delay toggle to ensure it happens after current click event completes
    // This prevents the document click listener from immediately closing the menu
    setTimeout(() => {
      this.menuOpen = !this.menuOpen;
    }, 0);
  }
  
  closeMenu() {
    this.menuOpen = false;
  }
  
  handleExplain() {
    this.dispatchEvent(new CustomEvent('info-explain'));
    this.closeMenu();
  }
  
  handleUpcoming() {
    this.dispatchEvent(new CustomEvent('info-upcoming'));
    this.closeMenu();
  }
  
  handleQuickMix() {
    this.dispatchEvent(new CustomEvent('info-quickmix'));
    this.closeMenu();
  }
  
  handleCreateStation() {
    this.dispatchEvent(new CustomEvent('info-create-station'));
    this.closeMenu();
  }
  
  handleAddMusic() {
    this.dispatchEvent(new CustomEvent('info-add-music'));
    this.closeMenu();
  }
  
  handleRenameStation() {
    this.dispatchEvent(new CustomEvent('info-rename-station'));
    this.closeMenu();
  }
  
  handleStationMode() {
    this.dispatchEvent(new CustomEvent('info-station-mode'));
    this.closeMenu();
  }
  
  handleStationSeeds() {
    this.dispatchEvent(new CustomEvent('info-station-seeds'));
    this.closeMenu();
  }
  
  handleDeleteStation() {
    this.dispatchEvent(new CustomEvent('info-delete-station'));
    this.closeMenu();
  }
  
  handleAddGenre() {
    this.dispatchEvent(new CustomEvent('info-add-genre'));
    this.closeMenu();
  }
  
  render() {
    return html`
      <div class="menu-popup ${this.menuOpen ? '' : 'hidden'}">
        <button class="action-button" @click=${this.handleExplain}>
          <span class="material-icons">help_outline</span>
          <span>Explain why this song is playing</span>
        </button>
        <button class="action-button" @click=${this.handleUpcoming}>
          <span class="material-icons">queue_music</span>
          <span>Show upcoming songs</span>
        </button>
        <button class="action-button" @click=${this.handleQuickMix}>
          <span class="material-icons">library_music</span>
          <span>Select QuickMix stations</span>
        </button>
        <button class="action-button" @click=${this.handleCreateStation}>
          <span class="material-icons">add_circle</span>
          <span>Create Station</span>
        </button>
        <button class="action-button" @click=${this.handleAddMusic}>
          <span class="material-icons">library_add</span>
          <span>Add Music to Station</span>
        </button>
        <button class="action-button" @click=${this.handleRenameStation}>
          <span class="material-icons">edit</span>
          <span>Rename Station</span>
        </button>
        <button class="action-button" @click=${this.handleStationMode}>
          <span class="material-icons">tune</span>
          <span>Manage Station Mode</span>
        </button>
        <button class="action-button" @click=${this.handleStationSeeds}>
          <span class="material-icons">manage_search</span>
          <span>Manage Seeds & Feedback</span>
        </button>
        <button class="action-button" @click=${this.handleAddGenre}>
          <span class="material-icons">library_music</span>
          <span>Add genre station</span>
        </button>
        <button class="action-button delete" @click=${this.handleDeleteStation}>
          <span class="material-icons">delete</span>
          <span>Delete station</span>
        </button>
      </div>
    `;
  }
}

