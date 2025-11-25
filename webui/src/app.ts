import { LitElement, html, css } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { SocketService } from './services/socket-service';

import './components/album-art';
import './components/progress-bar';
import './components/playback-controls';
import './components/volume-control';
import './components/reconnect-button';
import './components/bottom-toolbar';
import './components/toast-notification';
import './components/quickmix-modal';
import './components/create-station-modal';
import './components/play-station-modal';
import './components/select-station-modal';
import './components/genre-modal';
import './components/add-music-modal';
import './components/song-actions-menu';
import './components/info-menu';

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
  @state() private currentStationId = '';
  @state() private songStationName = '';
  @state() private quickMixModalOpen = false;
  @state() private createStationModalOpen = false;
  @state() private currentTrackToken = '';
  @state() private creatingStationFrom: 'song' | 'artist' | 'genre' | 'search' | null = null;
  @state() private newStationId: string | null = null;
  @state() private playNewStationModalOpen = false;
  @state() private newStationName = '';
  @state() private selectStationModalOpen = false;
  @state() private genreModalOpen = false;
  @state() private genreCategories: any[] = [];
  @state() private genreLoading = false;
  @state() private searchResults: any = { categories: [] };
  @state() private searchLoading = false;
  @state() private addMusicModalOpen = false;
  
  static styles = css`
    :host {
      display: block;
      min-height: 100vh;
      background: var(--background);
      color: var(--on-background);
      padding-bottom: 80px; /* Space for bottom toolbar */
    }
    
    .menu-container {
      position: fixed;
      top: 16px;
      right: 16px;
      z-index: 150;
    }
    
    .menu-button {
      width: 48px;
      height: 48px;
      border-radius: 50%;
      border: none;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
    }
    
    .menu-button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
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
      font-style: italic;
    }
    
    .controls-container {
      display: flex;
      justify-content: center;
      align-items: center;
      gap: 1rem;
      margin: 2rem 0;
    }
    
    .rating-container {
      position: relative;
      display: flex;
    }
    
    .rating-button {
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
    
    .rating-button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    .rating-button.loved {
      color: #4CAF50;
    }
    
    .rating-button.loved:hover {
      background: rgba(76, 175, 80, 0.1);
      color: #4CAF50;
    }
    
    .material-icons,
    .material-icons-outlined {
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
    
    .material-icons-outlined {
      font-family: 'Material Icons Outlined';
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
      this.currentTrackToken = data.trackToken || '';
      
      // Update current station if present
      if (data.station) {
        this.currentStation = data.station;
      }
      if (data.stationId) {
        this.currentStationId = data.stationId;
      }
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
      const oldStationIds = new Set(this.stations.map(s => s.id));
      this.stations = Array.isArray(data) ? data : [];
      
      // If we were creating a station, find the new one
      if (this.creatingStationFrom) {
        const newStation = this.stations.find(s => !oldStationIds.has(s.id));
        if (newStation) {
          this.newStationId = newStation.id;
          this.newStationName = newStation.name;
          this.playNewStationModalOpen = true;
          this.creatingStationFrom = null;
        }
      }
    });
    
    this.socket.on('genres', (data) => {
      this.genreCategories = data.categories || [];
      this.genreLoading = false;
    });
    
    this.socket.on('searchResults', (data) => {
      this.searchResults = data;
      this.searchLoading = false;
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
        this.currentTrackToken = data.song.trackToken || '';
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
        this.currentTrackToken = '';
      }
      
      // Update current station if present
      if (data.station) {
        this.currentStation = data.station;
      }
      if (data.stationId) {
        this.currentStationId = data.stationId;
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
    
    // Song explanation event
    this.socket.on('song.explanation', (data) => {
      console.log('Received explanation:', data);
      if (data.explanation) {
        this.showToast(data.explanation);
      }
    });
    
    // Upcoming songs event
    this.socket.on('query.upcoming.result', (data) => {
      console.log('Received upcoming songs:', data);
      if (Array.isArray(data) && data.length > 0) {
        this.showUpcomingSongsToast(data);
      } else {
        this.showToast('No upcoming songs in queue');
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
  
  toggleMenu() {
    const menu = this.shadowRoot?.querySelector('info-menu');
    if (menu) {
      (menu as any).toggleMenu();
    }
  }
  
  toggleRatingMenu() {
    const menu = this.shadowRoot?.querySelector('song-actions-menu');
    if (menu) {
      (menu as any).toggleMenu();
    }
  }
  
  handleInfoExplain() {
    this.socket.emit('action', 'song.explain');
  }
  
  handleInfoUpcoming() {
    this.socket.emit('action', 'query.upcoming');
  }
  
  handleInfoQuickMix() {
    this.quickMixModalOpen = true;
  }
  
  handleQuickMixSave(e: CustomEvent) {
    const { stationIds } = e.detail;
    this.socket.emit('station.setQuickMix', stationIds);
    this.quickMixModalOpen = false;
  }
  
  handleQuickMixCancel() {
    this.quickMixModalOpen = false;
  }
  
  handleInfoCreateStation() {
    this.createStationModalOpen = true;
  }
  
  handleCreateStationSong() {
    this.socket.emit('station.createFrom', {
      trackToken: this.currentTrackToken,
      type: 'song'
    });
    this.creatingStationFrom = 'song';
    this.createStationModalOpen = false;
  }
  
  handleCreateStationArtist() {
    this.socket.emit('station.createFrom', {
      trackToken: this.currentTrackToken,
      type: 'artist'
    });
    this.creatingStationFrom = 'artist';
    this.createStationModalOpen = false;
  }
  
  handleCreateStationCancel() {
    this.createStationModalOpen = false;
    this.searchResults = { categories: [] };
    this.searchLoading = false;
  }
  
  handleCreateStationQuery(e: CustomEvent) {
    this.searchLoading = true;
    this.socket.emit('music.search', { query: e.detail.query });
  }
  
  handleCreateStationFromSearch(e: CustomEvent) {
    const { musicId } = e.detail;
    this.socket.emit('station.addGenre', { musicId });
    this.creatingStationFrom = 'search';
    this.createStationModalOpen = false;
    this.searchResults = { categories: [] };
    this.searchLoading = false;
  }
  
  handlePlayNewStation() {
    if (this.newStationId) {
      this.socket.emit('station.change', this.newStationId);
    }
    this.playNewStationModalOpen = false;
    this.newStationId = null;
  }
  
  handleCancelPlayNewStation() {
    this.playNewStationModalOpen = false;
    this.newStationId = null;
    this.showToast(`Station "${this.newStationName}" created`);
  }
  
  handleInfoDeleteStation() {
    this.selectStationModalOpen = true;
  }
  
  handleStationSelectedForDelete(e: CustomEvent) {
    const station = this.stations.find(s => s.id === e.detail.stationId);
    if (station) {
      this.socket.emit('station.delete', station.id);
      this.showToast(`Deleting station "${station.name}"...`);
      this.selectStationModalOpen = false;
    }
  }
  
  handleCancelSelectStation() {
    this.selectStationModalOpen = false;
  }
  
  handleInfoAddGenre() {
    this.genreModalOpen = true;
    this.genreLoading = true;
    this.socket.emit('station.getGenres');
  }
  
  handleGenreCreate(e: CustomEvent) {
    const { musicId } = e.detail;
    this.socket.emit('station.addGenre', { musicId });
    this.creatingStationFrom = 'genre';
    this.genreModalOpen = false;
    this.genreCategories = [];
    this.genreLoading = false;
  }
  
  handleGenreCancel() {
    this.genreModalOpen = false;
    this.genreCategories = [];
    this.genreLoading = false;
  }
  
  handleInfoAddMusic() {
    this.addMusicModalOpen = true;
  }
  
  handleAddMusicSearch(e: CustomEvent) {
    this.searchLoading = true;
    this.socket.emit('music.search', { query: e.detail.query });
  }
  
  handleAddMusicSubmit(e: CustomEvent) {
    const { musicId, stationId } = e.detail;
    this.socket.emit('station.addMusic', { musicId, stationId });
    this.addMusicModalOpen = false;
    this.searchResults = { categories: [] };
    this.searchLoading = false;
    this.showToast('Adding music to station...');
  }
  
  handleAddMusicCancel() {
    this.addMusicModalOpen = false;
    this.searchResults = { categories: [] };
    this.searchLoading = false;
  }
  
  showToast(message: string) {
    const toast = document.createElement('toast-notification') as any;
    toast.message = message;
    toast.duration = 5000;
    document.body.appendChild(toast);
  }
  
  showUpcomingSongsToast(songs: any[]) {
    const formatDuration = (seconds: number) => {
      const mins = Math.floor(seconds / 60);
      const secs = seconds % 60;
      return `${mins}:${secs.toString().padStart(2, '0')}`;
    };
    
    const content = html`
      <div>
        <strong>Upcoming Songs</strong>
        <div class="song-list">
          ${songs.map((song, index) => html`
            <div class="song-item">
              <div class="song-number">${index + 1}</div>
              ${song.coverArt ? html`
                <img class="song-cover" src="${song.coverArt}" alt="${song.title}" />
              ` : ''}
              <div class="song-details">
                <p class="song-title">${song.title}</p>
                <p class="song-artist">${song.artist}</p>
                ${song.stationName ? html`
                  <p class="song-station">Station: ${song.stationName}</p>
                ` : ''}
              </div>
              ${song.rating === 1 ? html`
                <span class="material-icons">thumb_up</span>
              ` : ''}
              <div class="song-duration">${formatDuration(song.duration)}</div>
            </div>
          `)}
        </div>
      </div>
    `;
    
    const toast = document.createElement('toast-notification') as any;
    toast.content = content;
    toast.duration = 5000;
    document.body.appendChild(toast);
  }
  
  render() {
    return html`
      ${this.connected ? html`
        <div class="menu-container">
          <button class="menu-button" @click=${this.toggleMenu} title="Menu">
            <span class="material-icons">menu</span>
          </button>
          <info-menu
            @info-explain=${this.handleInfoExplain}
            @info-upcoming=${this.handleInfoUpcoming}
            @info-quickmix=${this.handleInfoQuickMix}
            @info-create-station=${this.handleInfoCreateStation}
            @info-add-music=${this.handleInfoAddMusic}
            @info-add-genre=${this.handleInfoAddGenre}
            @info-delete-station=${this.handleInfoDeleteStation}
          ></info-menu>
        </div>
      ` : ''}
      
      <album-art 
        src="${this.connected ? this.albumArt : ''}"
      ></album-art>
      
      <div class="song-info">
        <h1>${this.connected ? this.songTitle : 'Disconnected'}</h1>
        <p class="artist">${this.connected ? this.artistName : '—'}</p>
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
        
        <div class="controls-container">
          <div class="rating-container">
            <button 
              class="rating-button ${this.rating === 1 ? 'loved' : ''}"
              @click=${this.toggleRatingMenu}
              title="${this.rating === 1 ? 'Loved' : 'Rate this song'}"
            >
              <span class="${this.rating === 1 ? 'material-icons' : 'material-icons-outlined'}">
                ${this.rating === 1 ? 'thumb_up' : 'thumbs_up_down'}
              </span>
            </button>
            <song-actions-menu
              rating="${this.rating}"
              @love=${this.handleLove}
              @ban=${this.handleBan}
              @tired=${this.handleTired}
            ></song-actions-menu>
          </div>
          
          <playback-controls 
            ?playing="${this.playing}"
            @play=${this.handlePlayPause}
            @next=${this.handleNext}
          ></playback-controls>
        </div>
        
        <bottom-toolbar
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          currentStationId="${this.currentStationId}"
          songStationName="${this.songStationName}"
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
          @station-change=${this.handleStationChange}
          @info-explain=${this.handleInfoExplain}
          @info-upcoming=${this.handleInfoUpcoming}
          @info-quickmix=${this.handleInfoQuickMix}
          @info-create-station=${this.handleInfoCreateStation}
          @info-add-music=${this.handleInfoAddMusic}
          @info-add-genre=${this.handleInfoAddGenre}
          @info-delete-station=${this.handleInfoDeleteStation}
        ></bottom-toolbar>
        
        <quickmix-modal
          .stations="${this.stations}"
          ?open="${this.quickMixModalOpen}"
          @save=${this.handleQuickMixSave}
          @cancel=${this.handleQuickMixCancel}
        ></quickmix-modal>
        
        <create-station-modal
          ?open="${this.createStationModalOpen}"
          ?loading="${this.searchLoading}"
          .searchResults="${this.searchResults}"
          .currentSongName="${this.songTitle}"
          .currentArtistName="${this.artistName}"
          .currentTrackToken="${this.currentTrackToken}"
          @select-song=${this.handleCreateStationSong}
          @select-artist=${this.handleCreateStationArtist}
          @search=${this.handleCreateStationQuery}
          @create=${this.handleCreateStationFromSearch}
          @cancel=${this.handleCreateStationCancel}
        ></create-station-modal>
        
        <play-station-modal
          ?open="${this.playNewStationModalOpen}"
          stationName="${this.newStationName}"
          @play=${this.handlePlayNewStation}
          @cancel=${this.handleCancelPlayNewStation}
        ></play-station-modal>
        
        <select-station-modal
          ?open="${this.selectStationModalOpen}"
          title="Delete Station"
          confirmText="Delete"
          ?confirmDanger="${true}"
          .stations="${this.stations}"
          @station-select=${this.handleStationSelectedForDelete}
          @cancel=${this.handleCancelSelectStation}
        ></select-station-modal>
        
        <genre-modal
          ?open="${this.genreModalOpen}"
          ?loading="${this.genreLoading}"
          .categories="${this.genreCategories}"
          @create=${this.handleGenreCreate}
          @cancel=${this.handleGenreCancel}
        ></genre-modal>
        
        <add-music-modal
          ?open="${this.addMusicModalOpen}"
          ?loading="${this.searchLoading}"
          .stations="${this.stations}"
          .searchResults="${this.searchResults}"
          @search=${this.handleAddMusicSearch}
          @add-music=${this.handleAddMusicSubmit}
          @cancel=${this.handleAddMusicCancel}
        ></add-music-modal>
        
      ` : html`
        <reconnect-button 
          @reconnect=${this.handleReconnect}
        ></reconnect-button>
      `}
    `;
  }
}

