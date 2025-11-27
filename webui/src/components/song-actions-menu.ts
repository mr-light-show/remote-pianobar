import { LitElement, html, css } from 'lit';
import { customElement, state, property } from 'lit/decorators.js';

@customElement('song-actions-menu')
export class SongActionsMenu extends LitElement {
  @property({ type: Number }) rating = 0;
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
      position: relative;
      display: inline-block;
    }
    
    .menu-popup {
      position: absolute;
      bottom: 64px;
      left: 50%;
      transform: translateX(-50%);
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
      z-index: 100;
      min-width: 180px;
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
    
    .action-button.love:hover {
      background: var(--success);
      color: var(--on-success);
    }
    
    .action-button.love.active {
      background: var(--success);
      color: var(--on-success);
    }
    
    .action-button.ban:hover {
      background: var(--error);
      color: var(--on-error);
    }
    
    .action-button.tired:hover {
      background: var(--warning);
      color: var(--on-warning);
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
  
  handleLove() {
    this.dispatchEvent(new CustomEvent('love'));
    this.closeMenu();
  }
  
  handleBan() {
    this.dispatchEvent(new CustomEvent('ban'));
    this.closeMenu();
  }
  
  handleTired() {
    this.dispatchEvent(new CustomEvent('tired'));
    this.closeMenu();
  }
  
  render() {
    return html`
      <div class="menu-popup ${this.menuOpen ? '' : 'hidden'}">
        <button class="action-button love ${this.rating === 1 ? 'active' : ''}" @click=${this.handleLove}>
          <span class="material-icons">thumb_up</span>
          <span>Love Song</span>
        </button>
        <button class="action-button ban" @click=${this.handleBan}>
          <span class="material-icons">thumb_down</span>
          <span>Ban Song</span>
        </button>
        <button class="action-button tired" @click=${this.handleTired}>
          <span class="material-icons">snooze</span>
          <span>Snooze (1 month)</span>
        </button>
      </div>
    `;
  }
}

