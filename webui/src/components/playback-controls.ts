import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

@customElement('playback-controls')
export class PlaybackControls extends LitElement {
  @property({ type: Boolean }) playing = false;
  @property({ type: Boolean }) disabled = false;
  
  static styles = css`
    :host {
      display: flex;
      justify-content: center;
      gap: 1rem;
    }
    
    button {
      width: 56px;
      height: 56px;
      border-radius: 50%;
      border: none;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
    }
    
    button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    button.primary {
      background: var(--primary-color);
      color: var(--on-primary);
    }
    
    button.primary:hover {
      background: var(--primary-dark);
    }
    
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    
    button:disabled:hover {
      background: var(--surface-variant);
      color: var(--on-surface);
    }
    
    button.primary:disabled:hover {
      background: var(--primary-color);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 28px;
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
  
  handlePlay() {
    this.dispatchEvent(new CustomEvent('play'));
  }
  
  handleNext() {
    this.dispatchEvent(new CustomEvent('next'));
  }
  
  render() {
    return html`
      <button 
        @click=${this.handlePlay} 
        class="primary" 
        ?disabled=${this.disabled}
        title="${this.disabled ? 'Select a station first' : (this.playing ? 'Pause' : 'Play')}"
      >
        <span class="material-icons">${this.playing ? 'pause' : 'play_arrow'}</span>
      </button>
      <button 
        @click=${this.handleNext} 
        ?disabled=${this.disabled}
        title="${this.disabled ? 'Select a station first' : 'Next Song'}"
      >
        <span class="material-icons">skip_next</span>
      </button>
    `;
  }
}

