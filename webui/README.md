# Pianobar Web UI - Modern Lit-based Interface

## Overview

A modern, component-based web interface for pianobar built with:
- **Lit 3.x** - Lightweight web components (5KB)
- **Material Icons** - Google's icon set
- **Vite** - Fast build tool
- **Socket.IO** - Real-time WebSocket communication

## Features

- ✅ Album art display with fallback disc icon
- ✅ Real-time song progress bar
- ✅ Play/pause/skip controls
- ✅ Volume slider
- ✅ Dark mode (automatic based on browser preference)
- ✅ Mobile-responsive design
- ✅ Material Design look and feel

## Development

### Prerequisites
```bash
cd webui
npm install
```

### Development Server
```bash
npm run dev
```
Opens at http://localhost:3000 with live reload and proxy to pianobar WebSocket on port 8080.

### Production Build
```bash
npm run build
```
Outputs to `../dist/webui/` - these files are served by pianobar's HTTP server.

## Testing

### 1. Start pianobar with WebSocket enabled

Make sure your `~/.config/pianobar/config` includes:
```
websocket_enabled=1
websocket_port=8080
webui_path=./dist/webui
```

Then start pianobar:
```bash
cd /Users/khawes/stash/pianobar
PIANOBAR_DEBUG=8 ./pianobar
```

### 2. Open web UI

Open your browser to:
```
http://localhost:8080/
```

You should see:
- Album art or disc icon
- Song title and artist
- Progress bar updating every second
- Playback controls (play/pause/next)
- Volume slider

### 3. Test features

- **Play/Pause**: Click the center play button
- **Skip**: Click the skip forward button
- **Volume**: Drag the volume slider
- **Progress**: Watch it update in real-time every second
- **Dark Mode**: Toggle your browser's dark mode preference
- **Mobile**: Test on mobile device (responsive design)

## File Structure

```
webui/
├── index.html                     Entry point
├── package.json                   Dependencies
├── vite.config.js                 Build configuration
├── tsconfig.json                  TypeScript config
├── src/
│   ├── app.ts                     Main application component
│   ├── components/
│   │   ├── album-art.ts           Album art display
│   │   ├── progress-bar.ts        Progress bar (NEW!)
│   │   ├── playback-controls.ts   Play/pause/skip buttons
│   │   ├── volume-control.ts      Volume slider
│   │   └── stations-menu.ts       Station list (for future use)
│   ├── services/
│   │   └── socket-service.ts      Socket.IO wrapper
│   └── styles/
│       ├── theme.css               Material Design theme variables
│       └── global.css              Global styles
└── dist/                           Built files (generated)
```

## Bundle Size

- **Total**: ~70KB uncompressed, ~22KB gzipped
- **Lit**: 5KB
- **Socket.IO client**: ~40KB
- **App code**: ~25KB

Much smaller than React/Angular alternatives!

## Browser Support

- Chrome/Edge 90+
- Firefox 87+
- Safari 14+
- Mobile browsers (iOS Safari, Chrome Android)

## Future Enhancements

- [ ] Station switching UI
- [ ] Song love/hate/tired buttons
- [ ] Song history
- [ ] Upcoming songs queue
- [ ] Volume persistence
- [ ] Keyboard shortcuts
- [ ] Album art caching
- [ ] Offline mode

## Advantages of Lit

1. **Tiny**: Only 5KB (vs 40KB+ for React)
2. **Fast**: No virtual DOM overhead
3. **Standards**: Real web components
4. **Simple**: Easy to learn and maintain
5. **Reactive**: Automatic UI updates
6. **TypeScript**: First-class support

## Development Notes

- Uses Vite's dev server with proxy for WebSocket connections
- TypeScript with strict mode enabled
- Material Design 3 inspired theming
- CSS variables for easy customization
- Component-based architecture for maintainability

