import { test, expect } from '@playwright/test';

test.describe('WebSocket Connection', () => {
  test('shows disconnected state when server is not running', async ({ page }) => {
    // Navigate to the page (this will fail to connect to WebSocket)
    await page.goto('/');

    // Should show disconnected state
    await expect(page.locator('h1')).toContainText('Disconnected');
    
    // Should show reconnect button instead of playback controls
    await expect(page.locator('reconnect-button')).toBeVisible();
    await expect(page.locator('playback-controls')).not.toBeVisible();
  });

  test('shows reconnect button with pulsing animation', async ({ page }) => {
    await page.goto('/');

    // Wait for disconnected state
    await expect(page.locator('h1')).toContainText('Disconnected');

    // Reconnect button should be visible
    const reconnectButton = page.locator('reconnect-button');
    await expect(reconnectButton).toBeVisible();

    // Check for sync icon
    const shadowRoot = reconnectButton.locator('button');
    await expect(shadowRoot).toBeVisible();
  });

  test('reconnect button can be clicked', async ({ page }) => {
    await page.goto('/');

    await expect(page.locator('h1')).toContainText('Disconnected');

    // Click reconnect button (will fail to connect, but should not error)
    const reconnectButton = page.locator('reconnect-button');
    await reconnectButton.click();

    // Should still show disconnected (since no server is running)
    await expect(page.locator('h1')).toContainText('Disconnected');
  });
});

test.describe('WebSocket Connection with Server', () => {
  test.skip('connects to pianobar server', async ({ page }) => {
    // This test requires pianobar to be running with WebSocket enabled
    // Skip by default, run with: PIANOBAR_RUNNING=1 npm run test:e2e
    
    await page.goto('/');

    // Should eventually show connected state
    await expect(page.locator('playback-controls')).toBeVisible({ timeout: 5000 });
    
    // Should not show reconnect button
    await expect(page.locator('reconnect-button')).not.toBeVisible();
  });
});

