import { describe, it, expect, beforeEach } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/playback-controls';
import type { PlaybackControls } from '../../src/components/playback-controls';

describe('PlaybackControls', () => {
  let element: PlaybackControls;

  describe('rendering', () => {
    it('renders play button when not playing', async () => {
      element = await fixture(html`
        <playback-controls ?playing=${false}></playback-controls>
      `);

      const playButton = element.shadowRoot!.querySelector('.primary');
      expect(playButton).toBeTruthy();
      expect(playButton!.textContent).toContain('play_arrow');
    });

    it('renders pause button when playing', async () => {
      element = await fixture(html`
        <playback-controls ?playing=${true}></playback-controls>
      `);

      const playButton = element.shadowRoot!.querySelector('.primary');
      expect(playButton).toBeTruthy();
      expect(playButton!.textContent).toContain('pause');
    });

    it('always renders next button', async () => {
      element = await fixture(html`
        <playback-controls></playback-controls>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      expect(buttons.length).toBe(2); // play/pause + next (previous removed)
      
      const nextButton = Array.from(buttons).find(btn => 
        btn.textContent?.includes('skip_next')
      );
      expect(nextButton).toBeTruthy();
    });

    it('does not render previous button', async () => {
      element = await fixture(html`
        <playback-controls></playback-controls>
      `);

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const hasPrevious = Array.from(buttons).some(btn => 
        btn.textContent?.includes('skip_previous')
      );
      expect(hasPrevious).toBe(false);
    });
  });

  describe('events', () => {
    it('emits play event when play/pause button clicked', async () => {
      element = await fixture(html`
        <playback-controls ?playing=${false}></playback-controls>
      `);

      let eventFired = false;
      element.addEventListener('play', () => {
        eventFired = true;
      });

      const playButton = element.shadowRoot!.querySelector('.primary') as HTMLButtonElement;
      playButton.click();

      expect(eventFired).toBe(true);
    });

    it('emits next event when next button clicked', async () => {
      element = await fixture(html`
        <playback-controls></playback-controls>
      `);

      let eventFired = false;
      element.addEventListener('next', () => {
        eventFired = true;
      });

      const buttons = element.shadowRoot!.querySelectorAll('button');
      const nextButton = Array.from(buttons).find(btn => 
        btn.textContent?.includes('skip_next')
      ) as HTMLButtonElement;
      
      nextButton.click();

      expect(eventFired).toBe(true);
    });
  });

  describe('accessibility', () => {
    it('has appropriate button titles', async () => {
      element = await fixture(html`
        <playback-controls ?playing=${false}></playback-controls>
      `);

      const playButton = element.shadowRoot!.querySelector('.primary') as HTMLButtonElement;
      expect(playButton.title).toBe('Play');
    });

    it('updates button title based on state', async () => {
      element = await fixture(html`
        <playback-controls ?playing=${true}></playback-controls>
      `);

      const playButton = element.shadowRoot!.querySelector('.primary') as HTMLButtonElement;
      expect(playButton.title).toBe('Pause');
    });
  });
});

