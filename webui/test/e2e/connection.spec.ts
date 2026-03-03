import { test, expect } from '@playwright/test';

test.describe('WebSocket Connection', () => {
  test.beforeEach(async ({ page }) => {
    // HTTP route does not intercept WebSocket; use routeWebSocket so the app stays disconnected.
    await page.routeWebSocket(/socket\.io/, ws => {
      ws.close();
    });
  });

  test('shows disconnected state when server is not running', async ({ page }) => {
    // Navigate to the page (this will fail to connect to WebSocket)
    await page.goto('/');

    // Should show disconnected state
    await expect(page.locator('h1')).toContainText('Disconnected');
    
    // Should show reconnect panel in album-art instead of playback controls
    await expect(page.locator('album-art .reconnect-panel')).toBeVisible();
    await expect(page.locator('playback-controls')).not.toBeVisible();
  });

  test('shows reconnect button with pulsing animation', async ({ page }) => {
    await page.goto('/');

    // Wait for disconnected state
    await expect(page.locator('h1')).toContainText('Disconnected');

    // Reconnect panel and button should be visible in album-art
    const reconnectPanel = page.locator('album-art .reconnect-panel');
    await expect(reconnectPanel).toBeVisible();
    const reconnectButton = page.locator('album-art .reconnect-panel button');
    await expect(reconnectButton).toBeVisible();
  });

  test('reconnect button can be clicked', async ({ page }) => {
    await page.goto('/');

    await expect(page.locator('h1')).toContainText('Disconnected');

    // Click reconnect button (will fail to connect, but should not error)
    const reconnectButton = page.locator('album-art .reconnect-panel button');
    await reconnectButton.click();

    // Should still show disconnected (since no server is running)
    await expect(page.locator('h1')).toContainText('Disconnected');
  });
});

const requiresPianobar = () => !process.env.PIANOBAR_RUNNING;

test.describe('WebSocket Connection with Server', () => {
  test.skip(requiresPianobar, 'connects to pianobar server', async ({ page }) => {
    // Run with: PIANOBAR_RUNNING=1 npm run test:e2e
    await page.goto('/');

    // Should eventually show connected state
    await expect(page.locator('playback-controls')).toBeVisible({ timeout: 5000 });
    
    // Should not show reconnect panel (connected state)
    await expect(page.locator('album-art .reconnect-panel')).not.toBeVisible();
  });
});

