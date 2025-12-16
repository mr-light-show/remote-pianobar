import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/volume-control';
import type { VolumeControl } from '../../src/components/volume-control';

describe('VolumeControl', () => {
  describe('rendering', () => {
    it('renders volume icon', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const icon = element.shadowRoot!.querySelector('.material-icons');
      expect(icon).toBeTruthy();
      expect(icon!.textContent).toContain('volume_up');
    });

    it('renders slider input', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]');
      expect(slider).toBeTruthy();
    });

    it('renders volume value display', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay).toBeTruthy();
    });

    it('displays default volume of 50%', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('50%');
    });

    it('displays custom volume value', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="75"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('75%');
    });

    it('displays dB value for volume', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('dB');
    });

    it('displays positive dB with + sign', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="75"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('+');
    });

    it('displays negative dB without extra sign', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="25"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      const text = valueDisplay!.textContent!;
      // Should have the minus sign from the negative number, not a plus
      expect(text).toContain('-');
      expect(text).not.toContain('+-');
    });

    it('sets slider min to 0', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      expect(slider.min).toBe('0');
    });

    it('sets slider max to 100', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      expect(slider.max).toBe('100');
    });

    it('sets slider value to volume prop', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="60"></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      expect(slider.value).toBe('60');
    });
  });

  describe('slider interaction', () => {
    it('updates volume when slider is changed', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      slider.value = '75';
      slider.dispatchEvent(new Event('input'));
      await element.updateComplete;

      expect(element.volume).toBe(75);
    });

    it('updates display when slider is changed', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      slider.value = '75';
      slider.dispatchEvent(new Event('input'));
      await element.updateComplete;

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('75%');
    });

    it('dispatches volume-change event with percent', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      let eventDetail: any;
      element.addEventListener('volume-change', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      slider.value = '75';
      slider.dispatchEvent(new Event('input'));

      expect(eventDetail.percent).toBe(75);
    });

    it('dispatches volume-change event with dB value', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      let eventDetail: any;
      element.addEventListener('volume-change', ((e: CustomEvent) => {
        eventDetail = e.detail;
      }) as EventListener);

      const slider = element.shadowRoot!.querySelector('input[type="range"]') as HTMLInputElement;
      slider.value = '75';
      slider.dispatchEvent(new Event('input'));

      expect(eventDetail.db).toBeDefined();
      expect(typeof eventDetail.db).toBe('number');
    });
  });

  describe('updateFromDb method', () => {
    it('updates volume from dB value', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      element.updateFromDb(5);
      await element.updateComplete;

      // Volume should be updated (exact value depends on conversion)
      expect(element.volume).toBeGreaterThan(50);
    });

    it('updates display when updated from dB', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      const initialDisplay = element.shadowRoot!.querySelector('.volume-value');
      const initialText = initialDisplay!.textContent;

      element.updateFromDb(-10);
      await element.updateComplete;

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      // Volume should change from initial 0dB value
      expect(valueDisplay!.textContent).not.toBe(initialText);
      // Check it has negative dB
      expect(valueDisplay!.textContent).toContain('dB');
      expect(valueDisplay!.textContent).toContain('-');
    });

    it('handles negative dB values', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      element.updateFromDb(-20);
      await element.updateComplete;

      // Should result in volume less than 50
      expect(element.volume).toBeLessThan(50);
    });

    it('handles positive dB values', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      element.updateFromDb(5);
      await element.updateComplete;

      // Should result in volume greater than 50
      expect(element.volume).toBeGreaterThan(50);
    });

    it('handles zero dB', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="30"></volume-control>
      `);

      element.updateFromDb(0);
      await element.updateComplete;

      // 0 dB should correspond to 50% (middle of the slider)
      expect(element.volume).toBe(50);
    });
  });

  describe('maxGain prop', () => {
    it('uses default maxGain of 10', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="100"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('+10dB');
    });

    it('respects custom maxGain', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="100" maxGain="15"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('+15dB');
    });

    it('calculates dB correctly with custom maxGain', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="75" maxGain="20"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      // At 75%, should be 50% into the top half (0 to maxGain range)
      // 75% = 50% + (25% of remaining 50%) = halfway through top range = +10dB with maxGain=20
      expect(valueDisplay!.textContent).toContain('+10dB');
    });
  });

  describe('perceptual curve', () => {
    it('maps 0% to -40dB', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="0"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('-40dB');
    });

    it('maps 50% to 0dB', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('0dB');
    });

    it('maps 100% to +maxGain dB', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="100" maxGain="10"></volume-control>
      `);

      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('+10dB');
    });

    it('uses squared curve for bottom half', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="25"></volume-control>
      `);

      // 25% should be 50% through bottom half
      // Using squared curve: -40 * (1 - 0.5)^2 = -40 * 0.25 = -10dB
      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('-10dB');
    });

    it('uses linear curve for top half', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="75" maxGain="10"></volume-control>
      `);

      // 75% = 50% through top half = +5dB with maxGain=10
      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      expect(valueDisplay!.textContent).toContain('+5dB');
    });
  });

  describe('conversion roundtrip accuracy', () => {
    // Test that converting from percent → dB → percent results in the same (or very close) value
    // This ensures the dbToSlider() function correctly inverts sliderToDb()
    
    const testCases = [
      { percent: 0, expectedDb: -40, description: 'minimum volume' },
      { percent: 5, expectedDb: -32, description: 'very low volume' },
      { percent: 10, expectedDb: -26, description: 'low volume' },
      { percent: 15, expectedDb: -20, description: 'low-medium volume' },
      { percent: 20, expectedDb: -14, description: 'medium-low volume' },
      { percent: 25, expectedDb: -10, description: 'medium volume (bottom half)' },
      { percent: 30, expectedDb: -6, description: 'medium-high volume (bottom half)' },
      { percent: 40, expectedDb: -2, description: 'high volume (bottom half)' },
      { percent: 50, expectedDb: 0, description: 'unity gain (0dB)' },
      { percent: 60, expectedDb: 2, description: 'boosted volume (top half)' },
      { percent: 70, expectedDb: 4, description: 'high boost' },
      { percent: 75, expectedDb: 5, description: 'mid-range boost' },
      { percent: 80, expectedDb: 6, description: 'higher boost' },
      { percent: 90, expectedDb: 8, description: 'near-maximum boost' },
      { percent: 100, expectedDb: 10, description: 'maximum volume' },
    ];

    testCases.forEach(({ percent, expectedDb, description }) => {
      it(`${percent}% (${description}) converts correctly: ${percent}% → ${expectedDb}dB → ~${percent}%`, async () => {
        const element: VolumeControl = await fixture(html`
          <volume-control volume="${percent}"></volume-control>
        `);

        // Step 1: Convert percent to dB (via display)
        const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
        const displayText = valueDisplay!.textContent || '';
        
        // Extract dB value from display (format: "XX% (+Y dB)" or "XX% (-Y dB)")
        const dbMatch = displayText.match(/([+-]?\d+)dB/);
        expect(dbMatch).toBeTruthy();
        const actualDb = parseInt(dbMatch![1]);
        expect(actualDb).toBe(expectedDb);

        // Step 2: Convert dB back to percent using updateFromDb
        element.updateFromDb(expectedDb);
        await element.updateComplete;

        // Step 3: Verify roundtrip accuracy (allow 1% difference due to integer rounding)
        const roundtripPercent = element.volume;
        const diff = Math.abs(percent - roundtripPercent);
        expect(diff).toBeLessThanOrEqual(1);
      });
    });

    it('handles multiple roundtrips without drift', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="25"></volume-control>
      `);

      // Get initial dB value
      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      const displayText = valueDisplay!.textContent || '';
      const dbMatch = displayText.match(/([+-]?\d+)dB/);
      const db = parseInt(dbMatch![1]);

      // Perform multiple roundtrips: percent → dB → percent → dB → percent
      for (let i = 0; i < 5; i++) {
        element.updateFromDb(db);
        await element.updateComplete;
      }

      // Should still be at or very close to original value
      expect(Math.abs(25 - element.volume)).toBeLessThanOrEqual(1);
    });

    it('roundtrip with custom maxGain', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="75" maxGain="20"></volume-control>
      `);

      // With maxGain=20, 75% should be +10dB
      const valueDisplay = element.shadowRoot!.querySelector('.volume-value');
      const displayText = valueDisplay!.textContent || '';
      const dbMatch = displayText.match(/([+-]?\d+)dB/);
      const db = parseInt(dbMatch![1]);
      expect(db).toBe(10);

      // Convert back with same maxGain
      element.updateFromDb(10);
      await element.updateComplete;

      // Should return to 75%
      expect(element.volume).toBe(75);
    });

    it('handles edge case: -40dB (minimum)', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50"></volume-control>
      `);

      element.updateFromDb(-40);
      await element.updateComplete;

      expect(element.volume).toBe(0);
    });

    it('handles edge case: 0dB (unity)', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="25"></volume-control>
      `);

      element.updateFromDb(0);
      await element.updateComplete;

      expect(element.volume).toBe(50);
    });

    it('handles edge case: +maxGain dB (maximum)', async () => {
      const element: VolumeControl = await fixture(html`
        <volume-control volume="50" maxGain="10"></volume-control>
      `);

      element.updateFromDb(10);
      await element.updateComplete;

      expect(element.volume).toBe(100);
    });
  });
});

