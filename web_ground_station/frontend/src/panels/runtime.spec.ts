// @vitest-environment jsdom

import { flushPromises, mount } from '@vue/test-utils';
import { createPinia, setActivePinia } from 'pinia';
import { QDialog, Quasar } from 'quasar';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import * as gatewayApi from '../api/gateway';
import { ApiError } from '../api/gateway';
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
    store.$patch({ activeTaskId: 'case-1', airborneTaskId: 'case-1', commandLink: state.command, missionLoaded: state.loaded, missionRunning: state.running, airborneMissionRunning: state.running, ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: state.loaded, mission_running: state.running, last_accepted_sequence: 1, vision_armed: false } });
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

  it('keeps STOP enabled for authoritative running state without an active task', async () => {
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: null, airborneTaskId: 'airborne-case', commandLink: 'online', airborneMissionRunning: true });
    await wrapper.vm.$nextTick();
    expect(wrapper.get('[data-testid="stop-command"]').attributes('disabled')).toBeUndefined();
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

  it('confirms STOP before calling the command endpoint', async () => {
    const stopMission = vi.spyOn(gatewayApi, 'stopMission').mockResolvedValue({
      ok: true,
      message: '任务已停止',
      ack: {
        ok: true,
        message: '任务已停止',
        task_id: 'case-1',
        mission_loaded: true,
        mission_running: false,
        last_accepted_sequence: 4,
        vision_armed: false,
      },
    });
    const { store, wrapper } = mountCommandBar();
    store.$patch({ activeTaskId: 'case-1', airborneTaskId: 'case-1', commandLink: 'online', missionRunning: true, airborneMissionRunning: true });
    await wrapper.vm.$nextTick();
    await wrapper.get('[data-testid="stop-command"]').trigger('click');
    expect(stopMission).not.toHaveBeenCalled();
    expect(wrapper.getComponent(QDialog).props('modelValue')).toBe(true);
    (wrapper.vm as unknown as { confirmCommand: () => void }).confirmCommand();
    await flushPromises();
    expect(stopMission).toHaveBeenCalledOnce();
    expect(wrapper.get('[data-testid="command-feedback"]').text()).toContain('任务已停止');
  });

  it('shows an HTTP rejection without changing authoritative running state', async () => {
    vi.spyOn(gatewayApi, 'startMission').mockRejectedValue(
      new ApiError(409, 'mission_not_ready', '机载端尚未就绪'),
    );
    const { store, wrapper } = mountCommandBar();
    store.$patch({
      activeTaskId: 'case-1',
      commandLink: 'online',
      missionLoaded: true,
      missionRunning: false,
      ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: true, mission_running: false, last_accepted_sequence: 1, vision_armed: false },
    });
    await wrapper.vm.$nextTick();
    await wrapper.get('[data-testid="start-command"]').trigger('click');
    (wrapper.vm as unknown as { confirmCommand: () => void }).confirmCommand();
    await flushPromises();
    expect(wrapper.get('[data-testid="command-feedback"]').text()).toContain('机载端尚未就绪');
    expect(store.missionRunning).toBe(false);
  });

  it('shows loading and disables all commands while a request is pending', async () => {
    let resolveStart!: (value: Awaited<ReturnType<typeof gatewayApi.startMission>>) => void;
    vi.spyOn(gatewayApi, 'startMission').mockReturnValue(new Promise((resolve) => { resolveStart = resolve; }));
    const { store, wrapper } = mountCommandBar();
    store.$patch({
      activeTaskId: 'case-1',
      commandLink: 'online',
      missionLoaded: true,
      ack: { ok: true, message: 'ready', task_id: 'case-1', mission_loaded: true, mission_running: false, last_accepted_sequence: 1, vision_armed: false },
    });
    await wrapper.vm.$nextTick();
    await wrapper.get('[data-testid="start-command"]').trigger('click');
    (wrapper.vm as unknown as { confirmCommand: () => void }).confirmCommand();
    await wrapper.vm.$nextTick();
    for (const command of ['load', 'start', 'stop']) {
      expect(wrapper.get(`[data-testid="${command}-command"]`).attributes('disabled')).toBeDefined();
    }
    expect(wrapper.get('[data-testid="start-command"] .q-spinner').exists()).toBe(true);
    resolveStart({ ok: true, message: 'ok' });
    await flushPromises();
  });
});

describe('responsive operator shell', () => {
  it('lets the operator explicitly refresh the command link', async () => {
    const probe = vi.spyOn(gatewayApi, 'probeLink').mockResolvedValue({
      ok: true,
      ack: { ok: true, message: 'pong', task_id: '', mission_loaded: false, mission_running: false, last_accepted_sequence: 1, vision_armed: false },
    });
    const pinia = createPinia();
    setActivePinia(pinia);
    const wrapper = mount(App, { global: { plugins: [pinia, Quasar] } });

    await wrapper.get('[data-testid="refresh-link"]').trigger('click');
    await flushPromises();

    expect(probe).toHaveBeenCalledOnce();
    expect(useGroundStore().commandLink).toBe('online');
  });

  it('provides compact detection details without replacing or mutating the map DOM', async () => {
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: 1024 });
    Object.defineProperty(window, 'innerHeight', { configurable: true, value: 600 });
    const pinia = createPinia();
    setActivePinia(pinia);
    const store = useGroundStore();
    store.$patch({
      detectionTotals: { hare: 3, blue_ball: 1 },
      recentDetections: [{ animal_name: 'hare', cell_code: 'A3B4' }],
    });
    const wrapper = mount(App, {
      attachTo: document.body,
      global: {
        plugins: [pinia, Quasar],
        stubs: { QDialog: { template: '<div class="q-dialog-stub"><slot /></div>' } },
      },
    });
    expect(wrapper.get('[data-testid="status-bar"]').exists()).toBe(true);
    expect(wrapper.get('[data-testid="command-bar"]').exists()).toBe(true);
    expect(wrapper.get('[data-testid="console-rail"]').classes()).toContain('console-rail');

    const map = wrapper.get('svg.map-canvas');
    const mapElement = map.element;
    const mapViewBox = map.attributes('viewBox');
    const mapClass = map.attributes('class');
    const detectionEntry = wrapper.get('[data-testid="open-detections"]');
    expect(detectionEntry.attributes('aria-label')).toContain('检测');
    await detectionEntry.trigger('click');

    const details = wrapper.get('[data-testid="detection-dialog-content"]');
    expect(details.text()).toContain('hare');
    expect(details.text()).toContain('3');
    expect(details.text()).toContain('hare');
    expect(details.text()).toContain('A3B4');
    expect(wrapper.get('svg.map-canvas').element).toBe(mapElement);
    expect(wrapper.get('svg.map-canvas').attributes('viewBox')).toBe(mapViewBox);
    expect(wrapper.get('svg.map-canvas').attributes('class')).toBe(mapClass);
  });
});
