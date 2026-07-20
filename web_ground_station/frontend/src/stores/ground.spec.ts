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
  task_sync_state: 'unconfirmed',
  airborne_task_id: null,
  airborne_mission_loaded: false,
  airborne_mission_running: false,
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

  it('accepts an initial snapshot at sequence zero', () => {
    const store = useGroundStore();

    expect(store.applySnapshot(snapshot({ snapshot_seq: 0, current_cell: 'A8B1' }))).toBe(true);
    expect(store.currentCell).toBe('A8B1');
  });

  it('rejects a stale snapshot without replacing authoritative state', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ snapshot_seq: 20, current_cell: 'A8B1' }));

    expect(store.applySnapshot(snapshot({ snapshot_seq: 19, current_cell: 'A1B1' }))).toBe(false);
    expect(store.snapshotSeq).toBe(20);
    expect(store.currentCell).toBe('A8B1');
  });

  it('rejects an equal-sequence snapshot for a conflicting task', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ snapshot_seq: 20, active_task_id: 'case-1' }));

    expect(store.applySnapshot(snapshot({ snapshot_seq: 20, active_task_id: 'case-2' }))).toBe(
      false,
    );
    expect(store.activeTaskId).toBe('case-1');
  });

  it('accepts a newer snapshot for a new task', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ snapshot_seq: 20, active_task_id: 'case-1' }));

    expect(store.applySnapshot(snapshot({ snapshot_seq: 21, active_task_id: 'case-2' }))).toBe(
      true,
    );
    expect(store.activeTaskId).toBe('case-2');
    expect(store.snapshotSeq).toBe(21);
  });

  it('applies detection and summary increments without a browser refresh', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({
      active_task_id: 'case-1',
      plan: { task_id: 'case-1' },
      command_link: 'online',
      ack: {
        ok: true,
        message: 'started',
        task_id: 'case-1',
        mission_loaded: true,
        mission_running: true,
        last_accepted_sequence: 5,
        vision_armed: false,
      },
      task_sync_state: 'matched',
      airborne_task_id: 'case-1',
      airborne_mission_loaded: true,
      airborne_mission_running: true,
      mission_loaded: true,
      mission_running: true,
    }));

    store.applyEvent(event({
      seq: 1,
      event: 'detection',
      payload: { animal_name: 'hare', cell_code: 'A9B1', count: 2 },
    }));
    store.applyEvent(event({
      seq: 2,
      type: 'task_summary',
      event: 'summary',
      payload: { success: true, visited_waypoints: 60 },
    }));

    expect(store.detectionTotals).toEqual({ hare: 2 });
    expect(store.recentDetections).toHaveLength(1);
    expect(store.recentSummary).toMatchObject({ success: true });
    expect(store.missionRunning).toBe(false);
    expect(store.airborneMissionRunning).toBe(false);
    expect(store.airborneTaskId).toBe('case-1');
    expect(store.canPlan).toBe(true);
    expect(store.canLoad).toBe(true);
    expect(store.canStop).toBe(false);
  });

  it('applies a matching authoritative ACK event to command gates', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ active_task_id: 'case-1', plan: { task_id: 'case-1' } }));

    store.applyEvent(event({
      seq: 1,
      type: 'ack',
      event: null,
      payload: {
        ok: true,
        message: 'loaded',
        task_id: 'case-1',
        mission_loaded: true,
        mission_running: false,
        last_accepted_sequence: 9,
        vision_armed: false,
      },
    }));

    expect(store.airborneTaskId).toBe('case-1');
    expect(store.taskSyncState).toBe('matched');
    expect(store.missionLoaded).toBe(true);
    expect(store.canStart).toBe(true);
  });

  it('accepts a mismatched authoritative ACK event and preserves task isolation', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({
      active_task_id: 'display-plan',
      plan: { task_id: 'display-plan' },
      command_link: 'online',
    }));

    store.applyEvent(event({
      seq: 1,
      task_id: 'airborne-task',
      type: 'ack',
      event: null,
      payload: {
        ok: true,
        message: 'running',
        task_id: 'airborne-task',
        mission_loaded: true,
        mission_running: true,
        last_accepted_sequence: 10,
        vision_armed: true,
      },
    }));

    expect(store.airborneTaskId).toBe('airborne-task');
    expect(store.airborneMissionRunning).toBe(true);
    expect(store.taskSyncState).toBe('mismatch');
    expect(store.missionLoaded).toBe(false);
    expect(store.missionRunning).toBe(false);
    expect(store.canPlan).toBe(false);
    expect(store.canLoad).toBe(false);
    expect(store.canStart).toBe(false);
    expect(store.canStop).toBe(true);
  });

  it('normalizes mock telemetry progress without a browser refresh', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ active_task_id: 'case-1' }));

    store.applyEvent(event({
      seq: 1,
      payload: { current_cell: 'A8B1', visited_cells: 4 },
    }));

    expect(store.currentCell).toBe('A8B1');
    expect(store.visitedCount).toBe(4);
  });

  it('shows a resynchronizing command state while reconnecting', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({ command_link: 'online' }));

    store.markResyncing();

    expect(store.commandLink).toBe('resyncing');
  });

  it('keeps mismatched airborne running state separate and STOP available', () => {
    const store = useGroundStore();
    store.applySnapshot(snapshot({
      active_task_id: 'display-plan',
      task_sync_state: 'mismatch',
      airborne_task_id: 'airborne-task',
      airborne_mission_loaded: true,
      airborne_mission_running: true,
    }));

    expect(store.missionLoaded).toBe(false);
    expect(store.missionRunning).toBe(false);
    expect(store.canPlan).toBe(false);
    expect(store.canLoad).toBe(false);
    expect(store.canStart).toBe(false);
    expect(store.canStop).toBe(false);

    store.commandLink = 'online';
    expect(store.canStop).toBe(true);
  });
});
