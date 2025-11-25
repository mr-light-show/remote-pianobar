import { html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ModalBase } from './modal-base';

@customElement('play-station-modal')
export class PlayStationModal extends ModalBase {
  @property({ type: String }) stationName = '';
  
  constructor() {
    super();
    this.title = 'Station Created!';
  }
  
  handlePlay() {
    this.dispatchEvent(new CustomEvent('play'));
  }
  
  static styles = [
    ModalBase.styles,
    css`
    .modal {
      max-width: 400px;
    }
    
    .modal-body {
      padding: 24px;
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    
    .message {
      font-size: 16px;
      color: var(--on-surface);
      text-align: center;
      line-height: 1.5;
    }
    
    .station-name {
      font-weight: 600;
      color: var(--primary);
    }
    
    .buttons {
      display: flex;
      gap: 12px;
      margin-top: 8px;
    }
    
    button {
      flex: 1;
      padding: 14px 20px;
      font-size: 16px;
    }
  `
  ];
  
  render() {
    const body = html`
      <p class="message">
        Play <span class="station-name">${this.stationName}</span> now?
      </p>
      <div class="buttons">
        <button class="button-cancel" @click=${this.handleCancel}>
          Later
        </button>
        <button class="button-confirm" @click=${this.handlePlay}>
          Play Now
        </button>
      </div>
    `;
    
    return this.renderModal(body);
  }
}

