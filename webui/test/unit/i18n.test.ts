import { describe, it, expect } from 'vitest';
import { t, tf } from '../../src/i18n';

describe('i18n', () => {
  it('t returns key when missing from catalog', () => {
    expect(t('definitely.missing.key.xyz')).toBe('definitely.missing.key.xyz');
  });

  it('t returns value for known key', () => {
    expect(t('web.ui.cancel')).toBeTruthy();
    expect(t('web.ui.cancel').length).toBeGreaterThan(0);
  });

  it('tf replaces placeholders', () => {
    const s = tf('web.ui.toast_station_created', { name: 'My Station' });
    expect(s).toContain('My Station');
  });

  it('tf leaves unknown placeholders unchanged', () => {
    const s = tf('web.ui.menu', { notInString: 'x' });
    expect(s).toBe(t('web.ui.menu'));
  });
});
