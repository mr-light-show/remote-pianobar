import { LitElement, html, css } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { SocketService } from './services/socket-service';

import './components/album-art';
import './components/progress-bar';
import './components/playback-controls';
import './components/volume-control';
import './components/reconnect-button';
import './components/bottom-toolbar';

@customElement('pianobar-app')
export class PianobarApp extends LitElement {
  private socket = new SocketService();
  
  @state() private connected = false;
  @state() private albumArt = '';
  @state() private songTitle = 'Not Playing';
  @state() private artistName = '—';
  @state() private playing = false;
  @state() private currentTime = 0;
  @state() private totalTime = 0;
  @state() private volume = 0;
  @state() private maxGain = 10;
  @state() private rating = 0;
  @state() private stations: any[] = [];
  @state() private currentStation = '';
  @state() private songStationName = '';
  
  static styles = css`
    :host {
      display: block;
      min-height: 100vh;
      background: var(--background);
      color: var(--on-background);
      padding-bottom: 80px; /* Space for bottom toolbar */
    }
    
    .song-info {
      text-align: center;
      padding: 1rem 2rem;
      max-width: 32rem;
      margin: 0 auto;
    }
    
    h1 {
      font-size: 1.5rem;
      font-weight: 500;
      margin: 0.5rem 0;
    }
    
    .artist {
      color: var(--on-surface-variant);
      margin: 0;
    }
    
    .station-info {
      color: var(--on-surface-variant);
      font-size: 0.875rem;
      margin: 0.5rem 0 0 0;
    }
  `;
  
  connectedCallback() {
    super.connectedCallback();
    this.setupSocketListeners();
    this.setupConnectionListener();
  }
  
  setupConnectionListener() {
    this.socket.onConnectionChange((connected) => {
      this.connected = connected;
      
      if (!connected) {
        // Reset state when disconnected
        this.albumArt = '';
        this.playing = false;
        this.currentTime = 0;
        this.totalTime = 0;
      }
    });
  }
  
  setupSocketListeners() {
    this.socket.on('start', (data) => {
      this.albumArt = data.coverArt;
      this.songTitle = data.title;
      this.artistName = data.artist;
      this.totalTime = data.duration;
      this.playing = true;
      this.rating = data.rating || 0;
      this.songStationName = data.songStationName || '';
    });
    
    this.socket.on('stop', () => {
      console.log('Received stop event');
      
      // Reset UI state
      this.playing = false;
      this.currentTime = 0;
      this.totalTime = 0;
      // Keep song info visible until next song starts
    });
    
    this.socket.on('progress', (data) => {
      this.currentTime = data.elapsed;
      this.totalTime = data.duration;
    });
    
    // Volume control removed from UI
    // this.socket.on('volume', (data) => {
    //   this.volume = typeof data === 'number' ? data : data.volume || 100;
    // });
    
    this.socket.on('stations', (data) => {
      // Backend sends stations pre-sorted according to user's sort setting
      this.stations = Array.isArray(data) ? data : [];
    });
    
    this.socket.on('process', (data) => {
      console.log('Received process event:', data);
      
      // Update UI with current state
      if (data.song) {
        this.albumArt = data.song.coverArt || '';
        this.songTitle = data.song.title || 'Not Playing';
        this.artistName = data.song.artist || '—';
        this.totalTime = data.song.duration || 0;
        this.playing = data.playing || false;
        this.rating = data.song.rating || 0;
        this.songStationName = data.song.songStationName || '';
      } else {
        // No song playing
        this.albumArt = '';
        this.songTitle = 'Not Playing';
        this.artistName = '—';
        this.playing = false;
        this.currentTime = 0;
        this.totalTime = 0;
        this.rating = 0;
        this.songStationName = '';
      }
      
      // Update current station if present
      if (data.station) {
        this.currentStation = data.station;
      }
      
      // Update volume and maxGain from config
      if (data.volume !== undefined) {
        this.volume = data.volume;
      }
      if (data.maxGain !== undefined) {
        this.maxGain = data.maxGain;
      }
      
      // Update volume control if present
      const volumeControl = this.shadowRoot?.querySelector('volume-control');
      if (volumeControl && data.volume !== undefined) {
        (volumeControl as any).updateFromDb(data.volume);
      }
    });
  }
  
  handlePlayPause() {
    // Optimistic UI update for immediate feedback
    this.playing = !this.playing;
    this.socket.emit('action', 'playback.toggle');
  }
  
  handleNext() {
    this.socket.emit('action', 'playback.next');
  }
  
  handleLove() {
    this.socket.emit('action', 'song.love');
  }
  
  handleBan() {
    this.socket.emit('action', 'song.ban');
  }
  
  handleTired() {
    this.socket.emit('action', 'song.tired');
  }
  
  handleStationChange(e: CustomEvent) {
    const { station } = e.detail;
    this.socket.emit('station.change', station);
  }
  
  handleVolumeChange(e: CustomEvent) {
    const { percent, db } = e.detail;
    this.volume = db;  // Store dB for display
    // Send percentage to backend
    this.socket.emit('action', { action: 'volume.set', volume: percent });
  }
  
  handleReconnect() {
    this.socket.reconnect();
  }
  
  render() {
    return html`
      <album-art 
        src="${this.connected ? this.albumArt : ''}"
      ></album-art>
      
      <div class="song-info">
        <h1>${this.connected ? this.songTitle : 'Disconnected'}</h1>
        <p class="artist">${this.connected ? this.artistName : '—'}</p>
        ${this.songStationName ? html`
          <p class="station-info">From: ${this.songStationName}</p>
        ` : ''}
      </div>
      
      <progress-bar 
        current="${this.connected ? this.currentTime : 0}"
        total="${this.connected ? this.totalTime : 0}"
      ></progress-bar>
      
      ${this.connected ? html`
        <volume-control
          .volume="${50}"
          .maxGain="${this.maxGain}"
          @volume-change=${this.handleVolumeChange}
        ></volume-control>
        
        <playback-controls 
          ?playing="${this.playing}"
          @play=${this.handlePlayPause}
          @next=${this.handleNext}
        ></playback-controls>
        
        <bottom-toolbar
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
          @station-change=${this.handleStationChange}
        ></bottom-toolbar>
      ` : html`
        <reconnect-button 
          @reconnect=${this.handleReconnect}
        ></reconnect-button>
      `}
    `;
  }
}

