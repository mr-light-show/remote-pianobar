import { html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ModalBase } from './modal-base';
import { t } from '../i18n';

@customElement('play-station-modal')
export class PlayStationModal extends ModalBase {
  @property({ type: String }) stationName = '';
  
  constructor() {
    super();
    this.title = t('web.ui.play_station_title');
  }
  
  handlePlay() {
    this.dispatchEvent(new CustomEvent('play'));
  }
  
  static styles = [
    ...ModalBase.styles,
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
        ${t('web.ui.play_station_before')}<span class="station-name">${this.stationName}</span>${t('web.ui.play_station_after')}
      </p>
      <div class="buttons">
        <button class="button-cancel" @click=${this.handleCancel}>
          ${t('web.ui.later')}
        </button>
        <button class="button-confirm" @click=${this.handlePlay}>
          ${t('web.ui.play_now')}
        </button>
      </div>
    `;
    
    return this.renderModal(body);
  }
}

