// @vitest-environment jsdom

import { flushPromises, mount } from '@vue/test-utils';
import { createPinia, setActivePinia } from 'pinia';
import { QDialog, Quasar } from 'quasar';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import * as gatewayApi from '../api/gateway';
import App from '../App.vue';
import CommandBar from '../components/CommandBar.vue';
import { useGroundStore } from '../stores/ground';

vi.mock('../composables/useTelemetry', () => ({ useTelemetry: vi.fn() }));

const mountCommandBar = () => {
  const pinia = createPinia();
  setActivePinia(pinia);
  return {
    store: useGroundStore(),
    wrapper: mount(CommandBar, { attachTo: document.body, global: { plugins: [pinia, Quasar], stubs: { teleport: false } } }),
  };
};

describe('runtime command console', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    window.scrollTo = vi.fn();
  });

  it.each([
    [{ command: 'offline', loaded: true, running: false }, false, false],
    [{ command: 'online', loaded: true, running: false }, true, false],
    [{ command: 'online', loaded: true, running: true }, false, true],
  ])('gates START and STOP from server state', async (state, canStart, canStop) => {
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: 'case-1', commandLink: state.command, missionLoaded: state.loaded, missionRunning: state.running, ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: state.loaded, mission_running: state.running, last_accepted_sequence: 1, vision_armed: false } });
    await flushPromises();
    expect(wrapper.get('[data-testid="start-command"]').attributes('disabled') === undefined).toBe(canStart);
    expect(wrapper.get('[data-testid="stop-command"]').attributes('disabled') === undefined).toBe(canStop);
  });

  it.each(['resyncing', 'offline'])('disables START while command link is %s', async (commandLink) => {
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: 'case-1', commandLink, missionLoaded: true, ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: true, mission_running: false, last_accepted_sequence: 1, vision_armed: false } });
    await wrapper.vm.$nextTick();
    expect(wrapper.get('[data-testid="start-command"]').attributes('disabled')).toBeDefined();
  });

  it('disables START when the ACK belongs to another task', async () => {
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: 'case-1', commandLink: 'online', missionLoaded: true, ack: { ok: true, message: 'stale', task_id: 'case-0', mission_loaded: true, mission_running: false, last_accepted_sequence: 1, vision_armed: false } });
    await wrapper.vm.$nextTick();
    expect(wrapper.get('[data-testid="start-command"]').attributes('disabled')).toBeDefined();
  });

  it('confirms START and shows the HTTP response without optimistic running state', async () => {
    let resolveStart!: (value: Awaited<ReturnType<typeof gatewayApi.startMission>>) => void;
    vi.spyOn(gatewayApi, 'startMission').mockReturnValue(new Promise((resolve) => { resolveStart = resolve; }));
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: 'case-1', commandLink: 'online', missionLoaded: true, ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: true, mission_running: false, last_accepted_sequence: 1, vision_armed: false } });
    await wrapper.vm.$nextTick();
    await wrapper.get('[data-testid="start-command"]').trigger('click');
    expect(gatewayApi.startMission).not.toHaveBeenCalled();
    await wrapper.vm.$nextTick();
    expect(wrapper.getComponent(QDialog).props('modelValue')).toBe(true);
    (wrapper.vm as unknown as { confirmCommand: () => void }).confirmCommand();
    expect(store.missionRunning).toBe(false);
    resolveStart({ ok: true, message: '飞行任务已启动', ack: { ...store.ack!, message: '飞行任务已启动', mission_running: true } });
    await flushPromises();
    expect(wrapper.get('[data-testid="command-feedback"]').text()).toContain('飞行任务已启动');
    expect(store.missionRunning).toBe(true);
  });
});

describe('responsive operator shell', () => {
  it('keeps status, SVG map and command bar together at the minimum viewport', async () => {
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: 1024 });
    Object.defineProperty(window, 'innerHeight', { configurable: true, value: 600 });
    const pinia = createPinia();
    setActivePinia(pinia);
    const wrapper = mount(App, { attachTo: document.body, global: { plugins: [pinia, Quasar], stubs: { teleport: false } } });
    expect(wrapper.get('[data-testid="status-bar"]').exists()).toBe(true);
    expect(wrapper.get('svg.map-canvas').exists()).toBe(true);
    expect(wrapper.get('[data-testid="command-bar"]').exists()).toBe(true);
    expect(wrapper.get('[data-testid="console-shell"]').classes()).toContain('console-shell');
    await wrapper.get('[data-testid="open-details"]').trigger('click');
    expect(wrapper.getComponent(QDialog).props('modelValue')).toBe(true);
    expect(wrapper.get('[data-testid="map-region"]').exists()).toBe(true);
  });
});
