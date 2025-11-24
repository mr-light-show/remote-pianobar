import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

/**
 * Convert slider percentage (0-100) to decibels (-40 to maxGain)
 * Uses perceptual curve: squared for bottom half, linear for top half
 */
function sliderToDb(sliderPercent: number, maxGain: number = 10): number {
  if (sliderPercent <= 50) {
    // Bottom half: -40 to 0 dB
    const normalized = sliderPercent / 50;
    return -40 * Math.pow(1 - normalized, 2);
  } else {
    // Top half: 0 to maxGain dB
    const normalized = (sliderPercent - 50) / 50;
    return maxGain * normalized;
  }
}

/**
 * Convert decibels (-40 to maxGain) to slider percentage (0-100)
 */
function dbToSlider(db: number, maxGain: number = 10): number {
  if (db <= 0) {
    // Bottom half: -40 to 0 dB maps to 0-50%
    const normalized = Math.sqrt(1 - (db / -40));
    return normalized * 50;
  } else {
    // Top half: 0 to maxGain dB maps to 50-100%
    const normalized = db / maxGain;
    return 50 + (normalized * 50);
  }
}

@customElement('volume-control')
export class VolumeControl extends LitElement {
  @property({ type: Number }) volume = 50;
  @property({ type: Number }) maxGain = 10;
  
  static styles = css`
    :host {
      display: flex;
      align-items: center;
      gap: 1rem;
      max-width: 32rem;
      margin: 0 auto;
      padding: 0 2rem;
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 24px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      color: var(--on-surface);
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    input[type="range"] {
      flex: 1;
      height: 4px;
      -webkit-appearance: none;
      appearance: none;
      background: var(--surface-variant);
      border-radius: 2px;
      outline: none;
    }
    
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 16px;
      height: 16px;
      border-radius: 50%;
      background: var(--primary-color);
      cursor: pointer;
    }
    
    input[type="range"]::-moz-range-thumb {
      width: 16px;
      height: 16px;
      border-radius: 50%;
      background: var(--primary-color);
      cursor: pointer;
      border: none;
    }
    
    .volume-value {
      min-width: 3rem;
      text-align: right;
      color: var(--on-surface-variant);
    }
  `;
  
  handleVolumeChange(e: Event) {
    const target = e.target as HTMLInputElement;
    const sliderPercent = parseInt(target.value);
    this.volume = sliderPercent;
    
    // Calculate dB for display only
    const db = Math.round(sliderToDb(sliderPercent, this.maxGain));
    
    this.dispatchEvent(new CustomEvent('volume-change', {
      detail: { 
        percent: sliderPercent,
        db  // for display/local state only
      }
    }));
  }
  
  // Method to update from dB value (for incoming updates)
  updateFromDb(db: number) {
    this.volume = Math.round(dbToSlider(db, this.maxGain));
  }
  
  render() {
    const db = Math.round(sliderToDb(this.volume, this.maxGain));
    const dbDisplay = db >= 0 ? `+${db}` : `${db}`;
    
    return html`
      <span class="material-icons">volume_up</span>
      <input 
        type="range" 
        min="0" 
        max="100" 
        step="1"
        .value="${this.volume}"
        @input=${this.handleVolumeChange}
      >
      <span class="volume-value">${this.volume}% (${dbDisplay}dB)</span>
    `;
  }
}

