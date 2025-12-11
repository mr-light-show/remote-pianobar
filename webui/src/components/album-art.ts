import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';

@customElement('album-art')
export class AlbumArt extends LitElement {
  @property() src = '';
  @property() alt = 'Album Art';
  
  static styles = css`
    :host {
      display: block;
      max-width: 32rem;
      margin: 0 auto;
      aspect-ratio: 1 / 1;
    }
    
    img {
      width: 100%;
      height: 100%;
      object-fit: cover;
    }
    
    .disc-icon {
      width: 100%;
      height: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
      background: var(--surface);
      color: var(--on-surface);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 8rem;
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
  
  render() {
    return this.src
      ? html`<img src="${this.src}" alt="${this.alt}">`
      : html`<div class="disc-icon">
               <span class="material-icons">album</span>
             </div>`;
  }
}

