// @vitest-environment jsdom
import { createPinia, setActivePinia } from 'pinia';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import type { GroundSnapshot, WebEvent } from '../models/gateway';

const fetchSnapshot = vi.fn<() => Promise<GroundSnapshot>>();

vi.mock('../api/gateway', () => ({ fetchSnapshot }));
vi.mock('vue', async (importOriginal) => ({
  ...(await importOriginal<typeof import('vue')>()),
  onMounted: (callback: () => void) => callback(),
  onBeforeUnmount: vi.fn(),
}));

class FakeWebSocket {
  static instances: FakeWebSocket[] = [];
  onopen: (() => void) | null = null;
  onmessage: ((event: MessageEvent<string>) => void) | null = null;
  onclose: (() => void) | null = null;

  constructor(public readonly url: string) {
    FakeWebSocket.instances.push(this);
  }

  close() {}
}

const snapshot = (overrides: Partial<GroundSnapshot> = {}): GroundSnapshot => ({
  snapshot_seq: 41,
  timestamp_ms: 0,
  active_task_id: 'case-1',
  plan: null,
  command_link: 'online',
  telemetry_link: 'online',
  pid_link: 'unknown',
  ack: null,
  mission_loaded: true,
  mission_running: false,
  vision_armed: false,
  current_cell: 'A8B1',
  visited_count: 0,
  detection_totals: {},
  recent_detections: [],
  target_update: null,
  recent_summary: null,
  recent_error: null,
  recording_error: '',
  ...overrides,
});

const event = (overrides: Partial<WebEvent> = {}): WebEvent => ({
  schema: 'nuedc.web.v1',
  type: 'task_event',
  seq: 42,
  timestamp_ms: 1,
  task_id: 'case-1',
  event: 'telemetry',
  payload: { current_cell: 'A7B1' },
  ...overrides,
});

describe('useTelemetry', () => {
  beforeEach(() => {
    setActivePinia(createPinia());
    FakeWebSocket.instances = [];
    vi.stubGlobal('WebSocket', FakeWebSocket);
    fetchSnapshot.mockReset();
  });

  it('applies the reconnect snapshot before queued incremental events', async () => {
    let resolveSnapshot!: (value: GroundSnapshot) => void;
    fetchSnapshot.mockReturnValue(
      new Promise((resolve) => {
        resolveSnapshot = resolve;
      }),
    );
    const { useTelemetry } = await import('./useTelemetry');
    const { useGroundStore } = await import('../stores/ground');

    useTelemetry();
    const socket = FakeWebSocket.instances[0];
    socket.onopen?.();
    socket.onmessage?.(new MessageEvent('message', { data: JSON.stringify(event()) }));

    expect(useGroundStore().currentCell).toBeNull();
    resolveSnapshot(snapshot());
    await vi.waitFor(() => expect(useGroundStore().currentCell).toBe('A7B1'));
  });

  it('accepts the gateway snapshot envelope sent as the first websocket frame', async () => {
    fetchSnapshot.mockResolvedValue(snapshot({ snapshot_seq: 1, current_cell: null }));
    const { useTelemetry } = await import('./useTelemetry');
    const { useGroundStore } = await import('../stores/ground');

    useTelemetry();
    const socket = FakeWebSocket.instances[0];
    socket.onopen?.();
    await vi.waitFor(() => expect(fetchSnapshot).toHaveBeenCalled());
    socket.onmessage?.(
      new MessageEvent('message', {
        data: JSON.stringify({
          type: 'snapshot',
          snapshot: snapshot({ snapshot_seq: 2, current_cell: 'A6B1' }),
        }),
      }),
    );

    await vi.waitFor(() => expect(useGroundStore().currentCell).toBe('A6B1'));
  });

  it('does not let an older websocket snapshot replace the reconnect snapshot', async () => {
    fetchSnapshot.mockResolvedValue(snapshot({ snapshot_seq: 41, current_cell: 'A8B1' }));
    const { useTelemetry } = await import('./useTelemetry');
    const { useGroundStore } = await import('../stores/ground');

    useTelemetry();
    const socket = FakeWebSocket.instances[0];
    socket.onmessage?.(
      new MessageEvent('message', {
        data: JSON.stringify({
          type: 'snapshot',
          snapshot: snapshot({ snapshot_seq: 40, current_cell: 'A1B1' }),
        }),
      }),
    );
    socket.onopen?.();

    await vi.waitFor(() => expect(useGroundStore().currentCell).toBe('A8B1'));
  });
});
