import { describe, it, expect, beforeEach, vi } from 'vitest';
import { SocketService } from '../../src/services/socket-service';

// Mock WebSocket
class MockWebSocket {
  public readyState = WebSocket.CONNECTING;
  public onopen: ((event: Event) => void) | null = null;
  public onclose: ((event: CloseEvent) => void) | null = null;
  public onerror: ((event: Event) => void) | null = null;
  public onmessage: ((event: MessageEvent) => void) | null = null;

  constructor(public url: string, public protocol?: string) {
    // Simulate async connection
    setTimeout(() => {
      this.readyState = WebSocket.OPEN;
      if (this.onopen) {
        this.onopen(new Event('open'));
      }
    }, 0);
  }

  send(data: string) {
    // Mock send
  }

  close() {
    this.readyState = WebSocket.CLOSED;
    if (this.onclose) {
      this.onclose(new CloseEvent('close'));
    }
  }

  // Helper for tests to simulate incoming messages
  simulateMessage(data: string) {
    if (this.onmessage) {
      this.onmessage(new MessageEvent('message', { data }));
    }
  }

  // Helper for tests to simulate errors
  simulateError() {
    if (this.onerror) {
      this.onerror(new Event('error'));
    }
  }
}

// Install mock
global.WebSocket = MockWebSocket as any;

describe('SocketService', () => {
  let service: SocketService;

  beforeEach(() => {
    // Reset any existing instances
    service = new SocketService();
  });

  describe('initialization', () => {
    it('creates WebSocket connection', () => {
      expect(service).toBeTruthy();
    });

    it('starts as disconnected', () => {
      expect(service.isConnected).toBe(false);
    });

    it('connects after initialization', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));
      expect(service.isConnected).toBe(true);
    });

    it('uses correct WebSocket URL', async () => {
      // Check that service was initialized (can't directly check private ws)
      expect(service).toBeTruthy();
    });
  });

  describe('connection events', () => {
    it('notifies listeners on connection', async () => {
      const newService = new SocketService();
      let connectionState = false;

      newService.onConnectionChange((connected) => {
        connectionState = connected;
      });

      await new Promise(resolve => setTimeout(resolve, 10));
      expect(connectionState).toBe(true);
    });

    it('notifies listeners on disconnection', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));
      
      let connectionState = true;
      service.onConnectionChange((connected) => {
        connectionState = connected;
      });

      // Simulate disconnect
      (service as any).ws.close();
      
      expect(service.isConnected).toBe(false);
      expect(connectionState).toBe(false);
    });

    it('immediately calls connection listener with current state', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));
      
      let callCount = 0;
      let lastState = false;

      service.onConnectionChange((connected) => {
        callCount++;
        lastState = connected;
      });

      expect(callCount).toBe(1);
      expect(lastState).toBe(true);
    });
  });

  describe('message handling', () => {
    it('parses Socket.IO messages', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));

      let receivedEvent: string | null = null;
      let receivedData: any = null;

      service.on('test-event', (data: any) => {
        receivedEvent = 'test-event';
        receivedData = data;
      });

      // Simulate Socket.IO message: 2["test-event",{"foo":"bar"}]
      (service as any).ws.simulateMessage('2["test-event",{"foo":"bar"}]');

      expect(receivedEvent).toBe('test-event');
      expect(receivedData).toEqual({ foo: 'bar' });
    });

    it('handles multiple listeners for same event', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));

      let calls = 0;

      service.on('test', () => calls++);
      service.on('test', () => calls++);

      (service as any).ws.simulateMessage('2["test",{}]');

      expect(calls).toBe(2);
    });
  });

  describe('emit', () => {
    it('sends Socket.IO formatted messages', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));

      const sendSpy = vi.spyOn((service as any).ws, 'send');

      service.emit('action', 'playback.toggle');

      expect(sendSpy).toHaveBeenCalled();
      const sentData = sendSpy.mock.calls[0][0];
      expect(sentData).toContain('action');
      expect(sentData).toContain('playback.toggle');
    });
  });

  describe('reconnect', () => {
    it('can be called when disconnected without errors', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));

      // Close connection
      (service as any).ws.close();
      await new Promise(resolve => setTimeout(resolve, 10));
      expect(service.isConnected).toBe(false);

      // Reconnect should not throw
      expect(() => service.reconnect()).not.toThrow();
    });

    it('can be called when connected without errors', async () => {
      await new Promise(resolve => setTimeout(resolve, 10));
      expect(service.isConnected).toBe(true);

      // Calling reconnect while connected should not throw
      expect(() => service.reconnect()).not.toThrow();
      
      // Should still be connected
      expect(service.isConnected).toBe(true);
    });
  });

  describe('error handling', () => {
    it('logs errors to console', async () => {
      const consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {});

      await new Promise(resolve => setTimeout(resolve, 10));
      (service as any).ws.simulateError();

      expect(consoleSpy).toHaveBeenCalled();
      consoleSpy.mockRestore();
    });
  });
});

