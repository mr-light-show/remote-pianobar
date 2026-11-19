import { test, expect } from '@playwright/test';

const requiresPianobar = () => !process.env.PIANOBAR_RUNNING;

test.describe('Playback Controls', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
  });

  test('shows playback controls when connected', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    await expect(page.locator('playback-controls')).toBeVisible();
    
    // Should show play/pause button
    const controls = page.locator('playback-controls');
    await expect(controls).toBeVisible();
  });

  test('can toggle play/pause', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    const controls = page.locator('playback-controls');
    await expect(controls).toBeVisible();

    // Find play button
    const playButton = controls.locator('button').first();
    await playButton.click();

    // Button state should change (this would require mocking or actual server)
  });

  test('can skip to next track', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    const controls = page.locator('playback-controls');
    await expect(controls).toBeVisible();

    // Find next button (second button, as previous was removed)
    const nextButton = controls.locator('button').nth(1);
    await nextButton.click();

    // Track should change (requires actual server)
  });

  test('does not show previous button', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    const controls = page.locator('playback-controls');
    await expect(controls).toBeVisible();

    // Should only have 2 buttons (play/pause and next)
    const buttons = controls.locator('button');
    await expect(buttons).toHaveCount(2);
  });
});

test.describe('Progress Bar', () => {
  test('progress bar is always visible', async ({ page }) => {
    await page.goto('/');
    
    const progressBar = page.locator('progress-bar');
    await expect(progressBar).toBeVisible();
  });

  test('shows track progress when playing', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    await page.goto('/');

    const progressBar = page.locator('progress-bar');
    await expect(progressBar).toBeVisible();

    // Should show time display
    // (actual values depend on server state)
  });
});

test.describe('Volume Control', () => {
  test('volume control is visible when connected', async ({ page }) => {
    // Disconnected, the reconnect panel replaces the volume control.
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    await page.goto('/');
    
    const volumeControl = page.locator('volume-control');
    await expect(volumeControl).toBeVisible();
  });

  test('can adjust volume', async ({ page }) => {
    test.skip(!process.env.PIANOBAR_RUNNING, 'requires a running pianobar');
    await page.goto('/');

    const volumeControl = page.locator('volume-control');
    await expect(volumeControl).toBeVisible();

    // Interact with volume slider
    // (implementation depends on volume control component)
  });
});

