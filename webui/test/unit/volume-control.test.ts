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
});

