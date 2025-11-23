export class SocketService {
  private ws: WebSocket;
  private listeners: Map<string, Function[]> = new Map();
  private connectionListeners: Function[] = [];
  public isConnected = false;
  
  constructor() {
    this.connect();
  }
  
  private connect() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${protocol}//${window.location.host}/socket.io`;
    
    this.ws = new WebSocket(url, 'socketio');
    
    this.ws.onopen = () => {
      console.log('WebSocket connected');
      this.isConnected = true;
      this.notifyConnectionChange(true);
      
      // Request full state including stations on connect
      this.emit('query', null);
    };
    
    this.ws.onmessage = (event) => {
      try {
        const message = event.data;
        
        // Socket.IO format: "2[event, data]"
        // Packet type "2" = EVENT, followed by JSON array
        if (typeof message === 'string' && message.startsWith('2')) {
          const jsonStr = message.substring(1); // Strip packet type "2"
          const arr = JSON.parse(jsonStr);
          const eventName = arr[0];
          const eventData = arr[1];
          
          console.log('Socket.IO event:', eventName, eventData);
          
          const callbacks = this.listeners.get(eventName);
          if (callbacks) {
            callbacks.forEach(cb => cb(eventData));
          }
        }
      } catch (e) {
        console.error('Failed to parse Socket.IO message:', e, event.data);
      }
    };
    
    this.ws.onerror = (error) => {
      console.error('WebSocket error:', error);
    };
    
    this.ws.onclose = () => {
      console.log('WebSocket disconnected');
      this.isConnected = false;
      this.notifyConnectionChange(false);
    };
  }
  
  private notifyConnectionChange(connected: boolean) {
    this.connectionListeners.forEach(cb => cb(connected));
  }
  
  onConnectionChange(callback: (connected: boolean) => void) {
    this.connectionListeners.push(callback);
    // Immediately notify of current state
    callback(this.isConnected);
  }
  
  reconnect() {
    if (this.ws.readyState !== WebSocket.OPEN) {
      console.log('Attempting to reconnect...');
      // Close old socket if it exists
      if (this.ws) {
        this.ws.close();
      }
      
      // Create new connection
      this.connect();
    }
  }
  
  on(event: string, callback: (data: any) => void) {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, []);
    }
    this.listeners.get(event)!.push(callback);
  }
  
  emit(event: string, data: any) {
    if (this.ws.readyState === WebSocket.OPEN) {
      // Socket.IO format: "2[event, data]" where "2" is the EVENT packet type
      const message = '2' + JSON.stringify([event, data]);
      this.ws.send(message);
    }
  }
  
  disconnect() {
    this.ws.close();
  }
}

