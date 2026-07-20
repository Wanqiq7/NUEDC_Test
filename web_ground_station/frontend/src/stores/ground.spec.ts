import { createPinia, setActivePinia } from 'pinia';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/gateway';
import type { GroundSnapshot, WebEvent } from '../models/gateway';
import { useGroundStore } from './ground';

const snapshot = (overrides: Partial<GroundSnapshot> = {}): GroundSnapshot => ({
  snapshot_seq: 0,
  timestamp_ms: 0,
  active_task_id: null,
  plan: null,
  command_link: 'unknown',
  telemetry_link: 'unknown',
  pid_link: 'unknown',
  ack: null,
  mission_loaded: false,
  mission_running: false,
  vision_armed: false,
  current_cell: null,
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
  type: 'event',
  seq: 1,
  timestamp_ms: 0,
  task_id: 'case-1',
  event: 'telemetry',
  payload: {},
  ...overrides,
});

describe('ground store', () => {
  beforeEach(() => {
    setActivePinia(createPinia());
    vi.restoreAllMocks();
  });

  it('does not accept telemetry for another task', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ active_task_id: 'case-1' }));
    store.applyEvent(event({ task_id: 'old', payload: { current_cell: 'A1B1' } }));
    expect(store.currentCell).toBeNull();
  });

  it('does not optimistically mark a mission running', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      new Response(
        JSON.stringify({
          ok: true,
          message: 'ok',
          ack: {
            ok: true,
            message: 'start accepted',
            task_id: 'case-1',
            mission_loaded: true,
            mission_running: true,
            last_accepted_sequence: 9,
            vision_armed: true,
          },
        }),
        { status: 200 },
      ),
    );
    const store = useGroundStore();
    store.applySnapshot(snapshot({ active_task_id: 'case-1', mission_loaded: true }));

    const request = store.startMission();

    expect(store.missionRunning).toBe(false);
    await request;
    expect(store.missionRunning).toBe(true);
  });

  it('ignores an incremental event at or below the snapshot sequence', () => {
    const store = useGroundStore();
    store.applySnapshot(
      snapshot({ snapshot_seq: 41, active_task_id: 'case-1', current_cell: 'A8B1' }),
    );
    store.applyEvent(event({ seq: 40, payload: { current_cell: 'A1B1' } }));
    expect(store.currentCell).toBe('A8B1');
  });

  it('keeps authoritative state when a command fails', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(
      new ApiError(504, 'command_timeout', '命令状态未知'),
    );
    const store = useGroundStore();
    store.applySnapshot(snapshot({ mission_running: false }));

    await expect(store.startMission()).rejects.toMatchObject({
      errorCode: 'command_timeout',
    });
    expect(store.missionRunning).toBe(false);
  });
});
