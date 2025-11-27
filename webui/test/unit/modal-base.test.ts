import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import { LitElement } from 'lit';
import { customElement } from 'lit/decorators.js';
import '../../src/components/modal-base';
import { ModalBase } from '../../src/components/modal-base';

// Create a test implementation of ModalBase
@customElement('test-modal')
class TestModal extends ModalBase {
  onCancelCalled = false;
  
  protected onCancel() {
    this.onCancelCalled = true;
  }
  
  render() {
    const body = html`<div class="test-body">Test Content</div>`;
    const footer = this.renderStandardFooter('Confirm', false, false, () => {
      this.dispatchEvent(new CustomEvent('confirm'));
    });
    return this.renderModal(body, footer);
  }
}

describe('ModalBase', () => {
  describe('rendering', () => {
    it('is hidden when open is false', async () => {
      const element: TestModal = await fixture(html`
        <test-modal></test-modal>
      `);
      
      expect(element.hasAttribute('open')).toBe(false);
      expect(element.open).toBe(false);
    });
    
    it('is visible when open is true', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      const computedStyle = window.getComputedStyle(element);
      expect(computedStyle.display).not.toBe('none');
    });
    
    it('renders title when provided', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open title="Test Title"></test-modal>
      `);
      
      const title = element.shadowRoot!.querySelector('.modal-title');
      expect(title).toBeTruthy();
      expect(title!.textContent).toBe('Test Title');
    });
    
    it('does not render title section when title is empty', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      const header = element.shadowRoot!.querySelector('.modal-header');
      expect(header).toBeFalsy();
    });
    
    it('renders modal body content', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      const body = element.shadowRoot!.querySelector('.modal-body');
      expect(body).toBeTruthy();
      expect(body!.textContent).toContain('Test Content');
    });
    
    it('renders backdrop', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      const backdrop = element.shadowRoot!.querySelector('.backdrop');
      expect(backdrop).toBeTruthy();
    });
  });
  
  describe('backdrop interaction', () => {
    it('closes modal when backdrop is clicked', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      let cancelFired = false;
      element.addEventListener('cancel', () => {
        cancelFired = true;
      });
      
      const backdrop = element.shadowRoot!.querySelector('.backdrop') as HTMLElement;
      backdrop.click();
      
      expect(cancelFired).toBe(true);
    });
    
    it('does not close modal when modal content is clicked', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      let cancelFired = false;
      element.addEventListener('cancel', () => {
        cancelFired = true;
      });
      
      const modal = element.shadowRoot!.querySelector('.modal') as HTMLElement;
      modal.click();
      
      expect(cancelFired).toBe(false);
    });
  });
  
  describe('cancel handling', () => {
    it('calls onCancel when handleCancel is invoked', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      element.handleCancel();
      
      expect(element.onCancelCalled).toBe(true);
    });
    
    it('dispatches cancel event', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      let cancelFired = false;
      element.addEventListener('cancel', () => {
        cancelFired = true;
      });
      
      element.handleCancel();
      
      expect(cancelFired).toBe(true);
    });
    
    it('cancel button triggers cancel event', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      let cancelFired = false;
      element.addEventListener('cancel', () => {
        cancelFired = true;
      });
      
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel') as HTMLButtonElement;
      cancelButton.click();
      
      expect(cancelFired).toBe(true);
    });
  });
  
  describe('renderStandardFooter', () => {
    it('renders cancel and confirm buttons', async () => {
      const element: TestModal = await fixture(html`
        <test-modal open></test-modal>
      `);
      
      const cancelButton = element.shadowRoot!.querySelector('.button-cancel');
      const confirmButton = element.shadowRoot!.querySelector('.button-confirm');
      
      expect(cancelButton).toBeTruthy();
      expect(confirmButton).toBeTruthy();
      expect(confirmButton!.textContent?.trim()).toBe('Confirm');
    });
    
    it('disables confirm button when confirmDisabled is true', async () => {
      @customElement('test-modal-disabled')
      class TestModalDisabled extends ModalBase {
        render() {
          const body = html`<div>Test</div>`;
          const footer = this.renderStandardFooter('Save', true, false, () => {});
          return this.renderModal(body, footer);
        }
      }
      
      const element: TestModalDisabled = await fixture(html`
        <test-modal-disabled open></test-modal-disabled>
      `);
      
      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(confirmButton.disabled).toBe(true);
    });
    
    it('applies danger class when confirmDanger is true', async () => {
      @customElement('test-modal-danger')
      class TestModalDanger extends ModalBase {
        render() {
          const body = html`<div>Test</div>`;
          const footer = this.renderStandardFooter('Delete', false, true, () => {});
          return this.renderModal(body, footer);
        }
      }
      
      const element: TestModalDanger = await fixture(html`
        <test-modal-danger open></test-modal-danger>
      `);
      
      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      expect(confirmButton.classList.contains('button-danger')).toBe(true);
    });
    
    it('triggers onConfirm callback when confirm button clicked', async () => {
      let confirmCalled = false;
      
      @customElement('test-modal-confirm')
      class TestModalConfirm extends ModalBase {
        render() {
          const body = html`<div>Test</div>`;
          const footer = this.renderStandardFooter('OK', false, false, () => {
            confirmCalled = true;
          });
          return this.renderModal(body, footer);
        }
      }
      
      const element: TestModalConfirm = await fixture(html`
        <test-modal-confirm open></test-modal-confirm>
      `);
      
      const confirmButton = element.shadowRoot!.querySelector('.button-confirm') as HTMLButtonElement;
      confirmButton.click();
      
      expect(confirmCalled).toBe(true);
    });
  });
  
  describe('renderLoading', () => {
    it('renders loading message with default text', async () => {
      @customElement('test-modal-loading')
      class TestModalLoading extends ModalBase {
        render() {
          return this.renderModal(this.renderLoading());
        }
      }
      
      const element: TestModalLoading = await fixture(html`
        <test-modal-loading open></test-modal-loading>
      `);
      
      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading).toBeTruthy();
      expect(loading!.textContent).toBe('Loading...');
    });
    
    it('renders loading message with custom text', async () => {
      @customElement('test-modal-loading-custom')
      class TestModalLoadingCustom extends ModalBase {
        render() {
          return this.renderModal(this.renderLoading('Fetching data...'));
        }
      }
      
      const element: TestModalLoadingCustom = await fixture(html`
        <test-modal-loading-custom open></test-modal-loading-custom>
      `);
      
      const loading = element.shadowRoot!.querySelector('.loading');
      expect(loading!.textContent).toBe('Fetching data...');
    });
  });
});

