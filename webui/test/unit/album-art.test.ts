import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../../src/components/album-art';
import type { AlbumArt } from '../../src/components/album-art';

describe('AlbumArt', () => {
  describe('rendering', () => {
    it('displays image when src is provided', async () => {
      const element: AlbumArt = await fixture(html`
        <album-art src="https://example.com/album.jpg"></album-art>
      `);

      const img = element.shadowRoot!.querySelector('img');
      expect(img).toBeTruthy();
      expect(img?.src).toBe('https://example.com/album.jpg');
    });

    it('displays fallback icon when src is empty', async () => {
      const element: AlbumArt = await fixture(html`
        <album-art src=""></album-art>
      `);

      const icon = element.shadowRoot!.querySelector('.disc-icon');
      expect(icon).toBeTruthy();
      
      const materialIcon = element.shadowRoot!.querySelector('.material-icons');
      expect(materialIcon?.textContent).toContain('album');
    });

    it('displays fallback icon when src is not provided', async () => {
      const element: AlbumArt = await fixture(html`
        <album-art></album-art>
      `);

      const icon = element.shadowRoot!.querySelector('.disc-icon');
      expect(icon).toBeTruthy();
    });

    it('updates when src changes', async () => {
      const element: AlbumArt = await fixture(html`
        <album-art src=""></album-art>
      `);

      // Initially shows fallback
      let icon = element.shadowRoot!.querySelector('.disc-icon');
      expect(icon).toBeTruthy();

      // Update src
      element.src = 'https://example.com/new-album.jpg';
      await element.updateComplete;

      // Now shows image
      const img = element.shadowRoot!.querySelector('img');
      expect(img).toBeTruthy();
      expect(img?.src).toBe('https://example.com/new-album.jpg');
    });
  });

  describe('accessibility', () => {
    it('has appropriate alt text', async () => {
      const element: AlbumArt = await fixture(html`
        <album-art src="https://example.com/album.jpg"></album-art>
      `);

      const img = element.shadowRoot!.querySelector('img');
      expect(img?.alt).toBeTruthy();
    });
  });
});

