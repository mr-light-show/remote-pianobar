import { html, css } from 'lit';
import { customElement } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

@customElement('create-station-modal')
export class CreateStationModal extends ModalBase {
  constructor() {
    super();
    this.title = 'Create Station From';
  }
  
  handleSong() {
    this.dispatchEvent(new CustomEvent('select-song'));
  }
  
  handleArtist() {
    this.dispatchEvent(new CustomEvent('select-artist'));
  }
  
  static styles = [
    ModalBase.styles,
    css`
    .modal {
      max-width: 400px;
    }
    
    .modal-body {
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 8px;
    }
    
    .action-button {
      display: flex;
      align-items: center;
      gap: 12px;
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
    
    .action-button:hover {
      background: var(--surface-container-highest);
      transform: translateY(-1px);
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
    }
    
    .action-button.cancel {
      color: var(--error);
      background: transparent;
      border: 1px solid var(--outline);
    }
    
    .action-button.cancel:hover {
      background: rgba(255, 0, 0, 0.1);
      border-color: var(--error);
    }
  `
  ];
  
  render() {
    const body = html`
      <button class="action-button" @click=${this.handleSong}>
        <span class="material-icons">music_note</span>
        <span>Song</span>
      </button>
      <button class="action-button" @click=${this.handleArtist}>
        <span class="material-icons">person</span>
        <span>Artist</span>
      </button>
      <button class="action-button cancel" @click=${this.handleCancel}>
        <span class="material-icons">close</span>
        <span>Cancel</span>
      </button>
    `;
    
    return this.renderModal(body);
  }
}

