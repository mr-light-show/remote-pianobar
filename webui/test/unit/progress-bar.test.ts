import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/progress-bar';
import type { ProgressBar } from '../../src/components/progress-bar';

describe('ProgressBar', () => {
  describe('rendering', () => {
    it('renders with zero progress', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="0" total="0"></progress-bar>
      `);

      expect(element).toBeTruthy();
      const progressTrack = element.shadowRoot!.querySelector('.progress-track');
      expect(progressTrack).toBeTruthy();
    });

    it('displays correct progress percentage', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="60" total="180"></progress-bar>
      `);

      // 60/180 = 33.33%
      const fill = element.shadowRoot!.querySelector('.progress-fill') as HTMLElement;
      expect(fill.style.width).toContain('33');
    });

    it('displays formatted time', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="90" total="210"></progress-bar>
      `);

      const timeDisplay = element.shadowRoot!.querySelector('.time-display');
      expect(timeDisplay).toBeTruthy();
      expect(timeDisplay!.textContent).toContain('1:30'); // 90 seconds = 1:30
      expect(timeDisplay!.textContent).toContain('3:30'); // 210 seconds = 3:30
    });

    it('handles zero total time', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="0" total="0"></progress-bar>
      `);

      const fill = element.shadowRoot!.querySelector('.progress-fill') as HTMLElement;
      expect(fill.style.width).toBe('0%');
    });
  });

  describe('time formatting', () => {
    it('formats single digit seconds with leading zero', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="5" total="10"></progress-bar>
      `);

      const timeDisplay = element.shadowRoot!.querySelector('.time-display');
      expect(timeDisplay).toBeTruthy();
      expect(timeDisplay!.textContent).toContain('0:05');
    });

    it('formats minutes and seconds correctly', async () => {
      const element: ProgressBar = await fixture(html`
        <progress-bar current="125" total="245"></progress-bar>
      `);

      const timeDisplay = element.shadowRoot!.querySelector('.time-display');
      expect(timeDisplay).toBeTruthy();
      expect(timeDisplay!.textContent).toContain('2:05'); // 125 seconds
      expect(timeDisplay!.textContent).toContain('4:05'); // 245 seconds
    });
  });
});

