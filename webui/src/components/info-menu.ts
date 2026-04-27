import { LitElement, html, css, TemplateResult } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { t } from '../i18n';

/**
 * Hamburger menu aligned with docs/MENU_STATE_MATRIX.md.
 * Rows can be **disabled** (Web) when prerequisites fail instead of hidden — see app `_menuRow*` / `_menuDisabled*`.
 */
@customElement('info-menu')
export class InfoMenu extends LitElement {
  @property({ type: Boolean }) showAccountSwitch = false;
  @property({ type: Boolean }) showExplain = true;
  @property({ type: Boolean }) disabledExplain = false;
  @property({ type: Boolean }) showUpcoming = true;
  @property({ type: Boolean }) disabledUpcoming = false;
  @property({ type: Boolean }) showStationMode = true;
  @property({ type: Boolean }) disabledStationMode = false;
  @property({ type: Boolean }) showStationSeeds = true;
  @property({ type: Boolean }) disabledStationSeeds = false;
  @property({ type: Boolean }) showQuickMix = true;
  @property({ type: Boolean }) disabledQuickMix = false;
  @property({ type: Boolean }) showCreateStation = true;
  @property({ type: Boolean }) showAddMusic = true;
  @property({ type: Boolean }) disabledAddMusic = false;
  @property({ type: Boolean }) showRename = true;
  @property({ type: Boolean }) disabledRename = false;
  @property({ type: Boolean }) showDelete = true;
  @property({ type: Boolean }) disabledDelete = false;
  @property({ type: Boolean }) showSelectStation = true;
  @property({ type: Boolean }) disabledSelectStation = false;

  @state() private menuOpen = false;

  connectedCallback() {
    super.connectedCallback();
    document.addEventListener('click', this.handleClickOutside);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    document.removeEventListener('click', this.handleClickOutside);
  }

  private handleClickOutside = (event: MouseEvent) => {
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

    .action-button:hover:not(:disabled) {
      background: var(--surface-variant);
    }

    .action-button:disabled {
      opacity: 0.45;
      cursor: not-allowed;
    }

    .action-button.delete {
      color: var(--error);
    }

    .action-button.delete:hover:not(:disabled) {
      background: rgba(255, 0, 0, 0.1);
    }

    .action-button.delete:disabled {
      color: var(--on-surface);
      opacity: 0.45;
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

    .menu-divider {
      height: 1px;
      background: var(--outline);
      margin: 8px 0;
    }
  `;

  toggleMenu() {
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

  handleSelectStation() {
    this.dispatchEvent(new CustomEvent('info-select-station'));
    this.closeMenu();
  }

  handleSwitchAccount() {
    this.dispatchEvent(new CustomEvent('info-switch-account'));
    this.closeMenu();
  }

  private _divider(): TemplateResult {
    return html`<div class="menu-divider"></div>`;
  }

  render() {
    const accountParts: TemplateResult[] = [];
    if (this.showAccountSwitch) {
      accountParts.push(html`
        <button class="action-button" @click=${this.handleSwitchAccount}>
          <span class="material-icons">account_circle</span>
          <span>${t('web.ui.menu_switch_account')}</span>
        </button>
      `);
    }
    if (this.showSelectStation) {
      const d = this.disabledSelectStation;
      accountParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleSelectStation}
        >
          <span class="material-icons">radio</span>
          <span>${t('web.ui.menu_select_station')}</span>
        </button>
      `);
    }
    const accountSeg: TemplateResult | null =
      accountParts.length > 0 ? html`${accountParts}` : null;

    const npParts: TemplateResult[] = [];
    if (this.showExplain) {
      const d = this.disabledExplain;
      npParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleExplain}
        >
          <span class="material-icons">help_outline</span>
          <span>${t('web.ui.menu_explain_song')}</span>
        </button>
      `);
    }
    if (this.showUpcoming) {
      const d = this.disabledUpcoming;
      npParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleUpcoming}
        >
          <span class="material-icons">queue_music</span>
          <span>${t('web.ui.menu_upcoming')}</span>
        </button>
      `);
    }
    if (this.showStationMode) {
      const d = this.disabledStationMode;
      npParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleStationMode}
        >
          <span class="material-icons">tune</span>
          <span>${t('web.ui.menu_station_mode')}</span>
        </button>
      `);
    }
    if (this.showStationSeeds) {
      const d = this.disabledStationSeeds;
      npParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleStationSeeds}
        >
          <span class="material-icons">manage_search</span>
          <span>${t('web.ui.menu_seeds_feedback')}</span>
        </button>
      `);
    }
    const nowPlayingSeg: TemplateResult | null =
      npParts.length > 0 ? html`${npParts}` : null;

    const libParts: TemplateResult[] = [];
    if (this.showQuickMix) {
      const d = this.disabledQuickMix;
      libParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleQuickMix}
        >
          <span class="material-icons">library_music</span>
          <span>${t('web.ui.menu_quickmix')}</span>
        </button>
      `);
    }
    if (this.showCreateStation) {
      libParts.push(html`
        <button class="action-button" @click=${this.handleCreateStation}>
          <span class="material-icons">add_circle</span>
          <span>${t('web.ui.menu_create_station')}</span>
        </button>
      `);
    }
    if (this.showAddMusic) {
      const d = this.disabledAddMusic;
      libParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleAddMusic}
        >
          <span class="material-icons">library_add</span>
          <span>${t('web.ui.menu_add_music')}</span>
        </button>
      `);
    }
    if (this.showRename) {
      const d = this.disabledRename;
      libParts.push(html`
        <button
          class="action-button"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleRenameStation}
        >
          <span class="material-icons">edit</span>
          <span>${t('web.ui.menu_rename_station')}</span>
        </button>
      `);
    }
    if (this.showDelete) {
      const d = this.disabledDelete;
      libParts.push(html`
        <button
          class="action-button delete"
          ?disabled=${d}
          title=${d ? t('web.ui.menu_item_unavailable') : ''}
          @click=${this.handleDeleteStation}
        >
          <span class="material-icons">delete</span>
          <span>${t('web.ui.menu_delete_station')}</span>
        </button>
      `);
    }
    const librarySeg: TemplateResult | null =
      libParts.length > 0 ? html`${libParts}` : null;

    const segments: TemplateResult[] = [accountSeg, nowPlayingSeg, librarySeg].filter(
      (s): s is TemplateResult => s !== null
    );

    return html`
      <div class="menu-popup ${this.menuOpen ? '' : 'hidden'}">
        ${segments.map((seg, i) => html`${i ? this._divider() : ''}${seg}`)}
      </div>
    `;
  }
}
