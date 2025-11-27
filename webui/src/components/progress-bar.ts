import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

@customElement('progress-bar')
export class ProgressBar extends LitElement {
  @property({ type: Number }) current = 0;
  @property({ type: Number }) total = 0;
  
  static styles = css`
    :host {
      display: block;
      max-width: 32rem;
      margin: 1rem auto;
      padding: 0 1rem;
    }
    
    .progress-track {
      height: 4px;
      background: var(--surface-variant);
      border-radius: 2px;
      overflow: hidden;
      margin-bottom: 0.5rem;
    }
    
    .progress-fill {
      height: 100%;
      background: var(--primary-color);
      transition: width 0.3s ease;
    }
    
    .time-display {
      display: flex;
      justify-content: space-between;
      font-size: 0.875rem;
      color: var(--on-surface-variant);
    }
  `;
  
  formatTime(seconds: number): string {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  }
  
  get progress(): number {
    return this.total > 0 ? (this.current / this.total) * 100 : 0;
  }
  
  render() {
    return html`
      <div class="progress-track">
        <div class="progress-fill" style="width: ${this.progress}%"></div>
      </div>
      <div class="time-display">
        <span>${this.formatTime(this.current)}</span>
        <span>${this.formatTime(this.total)}</span>
      </div>
    `;
  }
}

