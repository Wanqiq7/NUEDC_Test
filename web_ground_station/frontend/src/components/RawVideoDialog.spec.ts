// @vitest-environment jsdom
import { flushPromises, mount } from '@vue/test-utils';
import { createPinia, setActivePinia } from 'pinia';
import { Quasar } from 'quasar';
import { describe, expect, it, vi } from 'vitest';

import RawVideoDialog from './RawVideoDialog.vue';
import StatusBar from './StatusBar.vue';

const videoClient = vi.hoisted(() => ({
  open: vi.fn(async () => undefined),
  close: vi.fn(async () => undefined),
}));

vi.mock('../composables/useRawVideo', async () => {
  const { ref } = await import('vue');
  return { useRawVideo: () => ({ ...videoClient, state: ref('connecting') }) };
});

const dialogStub = {
  props: ['modelValue'],
  template: '<div v-if="modelValue" class="q-dialog-stub"><slot /></div>',
};

describe('RawVideoDialog', () => {
  it('opens a silent inline video surface and releases it when closed', async () => {
    const wrapper = mount(RawVideoDialog, {
      props: { modelValue: true },
      attachTo: document.body,
      global: { plugins: [Quasar], stubs: { QDialog: dialogStub } },
    });
    await flushPromises();

    const video = wrapper.get('[data-testid="raw-video"]');
    expect(video.attributes('autoplay')).toBeDefined();
    expect(video.attributes('playsinline')).toBeDefined();
    expect((video.element as HTMLVideoElement).muted).toBe(true);
    expect(videoClient.open).toHaveBeenCalledWith(video.element);
    expect(wrapper.get('[data-testid="raw-video-dialog"]').classes()).toContain('raw-video-dialog');
    expect(wrapper.text()).toContain('正在连接');

    await wrapper.setProps({ modelValue: false });
    await flushPromises();
    expect(videoClient.close).toHaveBeenCalled();
  });

  it('exposes an always-enabled camera action in the top status bar', async () => {
    const pinia = createPinia();
    setActivePinia(pinia);
    const wrapper = mount(StatusBar, {
      attachTo: document.body,
      global: { plugins: [pinia, Quasar], stubs: { QDialog: dialogStub } },
    });

    const action = wrapper.get('[data-testid="open-raw-video"]');
    expect(action.attributes('aria-label')).toBe('查看原始图传');
    expect(action.attributes('disabled')).toBeUndefined();
    await action.trigger('click');
    expect(wrapper.find('[data-testid="raw-video-dialog"]').exists()).toBe(true);
  });
});
