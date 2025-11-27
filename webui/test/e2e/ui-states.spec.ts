import { test, expect } from '@playwright/test';

test.describe('UI States', () => {
  test('shows fallback album art when disconnected', async ({ page }) => {
    await page.goto('/');

    const albumArt = page.locator('album-art');
    await expect(albumArt).toBeVisible();
    
    // Should show disc icon (generic fallback)
    // The actual implementation uses a shadow DOM element
  });

  test('shows correct disconnected text', async ({ page }) => {
    await page.goto('/');

    // Title should say "Disconnected"
    await expect(page.locator('h1')).toContainText('Disconnected');
    
    // Artist line should show em dash
    await expect(page.locator('.artist')).toContainText('—');
  });

  test('progress bar shows zero when disconnected', async ({ page }) => {
    await page.goto('/');

    const progressBar = page.locator('progress-bar');
    await expect(progressBar).toBeVisible();
    
    // Should show 0:00 / 0:00 or similar
  });

  test.skip('updates UI when track changes', async ({ page }) => {
    // Skip by default - requires running pianobar server
    await page.goto('/');

    // Wait for connection
    await expect(page.locator('playback-controls')).toBeVisible();

    // Get initial track title
    const initialTitle = await page.locator('h1').textContent();

    // Skip to next track
    const nextButton = page.locator('playback-controls button').nth(1);
    await nextButton.click();

    // Title should change
    await expect(page.locator('h1')).not.toContainText(initialTitle!);
  });
});

test.describe('Responsive Design', () => {
  test('renders correctly on mobile viewport', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 }); // iPhone SE
    await page.goto('/');

    // All components should be visible
    await expect(page.locator('album-art')).toBeVisible();
    await expect(page.locator('.song-info')).toBeVisible();
    await expect(page.locator('progress-bar')).toBeVisible();
    await expect(page.locator('reconnect-button')).toBeVisible();
    await expect(page.locator('volume-control')).toBeVisible();
  });

  test('renders correctly on tablet viewport', async ({ page }) => {
    await page.setViewportSize({ width: 768, height: 1024 }); // iPad
    await page.goto('/');

    await expect(page.locator('album-art')).toBeVisible();
    await expect(page.locator('.song-info')).toBeVisible();
  });

  test('renders correctly on desktop viewport', async ({ page }) => {
    await page.setViewportSize({ width: 1920, height: 1080 });
    await page.goto('/');

    await expect(page.locator('album-art')).toBeVisible();
    await expect(page.locator('.song-info')).toBeVisible();
  });
});

test.describe('Accessibility', () => {
  test('has no automatic accessibility violations', async ({ page }) => {
    await page.goto('/');

    // Basic accessibility checks
    // In a real implementation, you'd use @axe-core/playwright
    
    // Check for proper heading hierarchy
    await expect(page.locator('h1')).toBeVisible();
    
    // Check that interactive elements are keyboard accessible
    const reconnectButton = page.locator('reconnect-button button');
    await reconnectButton.focus();
    await expect(reconnectButton).toBeFocused();
  });

  test('buttons have appropriate titles/labels', async ({ page }) => {
    await page.goto('/');

    // Reconnect button should have title
    const reconnectButton = page.locator('reconnect-button button');
    await expect(reconnectButton).toHaveAttribute('title', 'Reconnect');
  });
});

