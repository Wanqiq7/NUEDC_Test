import { computed, ref } from 'vue';
import { defineStore } from 'pinia';

import * as api from '../api/gateway';
import type {
  AckSnapshot,
  CommandResponse,
  DynamicPayload,
  GroundSnapshot,
  PlanMissionRequest,
  WebEvent,
} from '../models/gateway';

function isAckSnapshot(payload: DynamicPayload): payload is DynamicPayload & AckSnapshot {
  return (
    typeof payload.ok === 'boolean' &&
    typeof payload.message === 'string' &&
    typeof payload.task_id === 'string' &&
    typeof payload.mission_loaded === 'boolean' &&
    typeof payload.mission_running === 'boolean' &&
    typeof payload.last_accepted_sequence === 'number' &&
    Number.isInteger(payload.last_accepted_sequence) &&
    typeof payload.vision_armed === 'boolean'
  );
}

export const useGroundStore = defineStore('ground', () => {
  const snapshotSeq = ref(0);
  const timestampMs = ref(0);
  const activeTaskId = ref<string | null>(null);
  const plan = ref<DynamicPayload | null>(null);
  const commandLink = ref('unknown');
  const telemetryLink = ref('unknown');
  const pidLink = ref('unknown');
  const ack = ref<AckSnapshot | null>(null);
  const taskSyncState = ref<'unconfirmed' | 'matched' | 'mismatch'>('unconfirmed');
  const airborneTaskId = ref<string | null>(null);
  const airborneMissionLoaded = ref(false);
  const airborneMissionRunning = ref(false);
  const missionLoaded = ref(false);
  const missionRunning = ref(false);
  const visionArmed = ref(false);
  const currentCell = ref<string | null>(null);
  const visitedCount = ref(0);
  const detectionTotals = ref<Record<string, number>>({});
  const recentDetections = ref<DynamicPayload[]>([]);
  const targetUpdate = ref<DynamicPayload | null>(null);
  const recentSummary = ref<DynamicPayload | null>(null);
  const recentError = ref<DynamicPayload | null>(null);
  const recordingError = ref('');
  const hasSnapshot = ref(false);

  const state = computed(() => ({
    snapshotSeq: snapshotSeq.value,
    timestampMs: timestampMs.value,
    activeTaskId: activeTaskId.value,
    plan: plan.value,
    commandLink: commandLink.value,
    telemetryLink: telemetryLink.value,
    pidLink: pidLink.value,
    ack: ack.value,
    taskSyncState: taskSyncState.value,
    airborneTaskId: airborneTaskId.value,
    airborneMissionLoaded: airborneMissionLoaded.value,
    airborneMissionRunning: airborneMissionRunning.value,
    missionLoaded: missionLoaded.value,
    missionRunning: missionRunning.value,
    visionArmed: visionArmed.value,
    currentCell: currentCell.value,
    visitedCount: visitedCount.value,
    detectionTotals: detectionTotals.value,
    recentDetections: recentDetections.value,
    targetUpdate: targetUpdate.value,
    recentSummary: recentSummary.value,
    recentError: recentError.value,
    recordingError: recordingError.value,
  }));
  const canPlan = computed(
    () => commandLink.value === 'online' && !airborneMissionRunning.value,
  );
  const canLoad = computed(
    () =>
      commandLink.value === 'online' &&
      !airborneMissionRunning.value &&
      plan.value !== null &&
      activeTaskId.value !== null,
  );
  const canStart = computed(
    () =>
      commandLink.value === 'online' &&
      activeTaskId.value !== null &&
      missionLoaded.value &&
      !missionRunning.value &&
      ack.value !== null &&
      ack.value.task_id === activeTaskId.value,
  );
  const canStop = computed(
    () =>
      commandLink.value === 'online' &&
      airborneMissionRunning.value &&
      airborneTaskId.value !== null,
  );

  function applySnapshot(snapshot: GroundSnapshot): boolean {
    if (hasSnapshot.value && snapshot.snapshot_seq <= snapshotSeq.value) {
      return false;
    }

    snapshotSeq.value = snapshot.snapshot_seq;
    timestampMs.value = snapshot.timestamp_ms;
    activeTaskId.value = snapshot.active_task_id;
    plan.value = snapshot.plan;
    commandLink.value = snapshot.command_link;
    telemetryLink.value = snapshot.telemetry_link;
    pidLink.value = snapshot.pid_link;
    ack.value = snapshot.ack;
    taskSyncState.value = snapshot.task_sync_state;
    airborneTaskId.value = snapshot.airborne_task_id;
    airborneMissionLoaded.value = snapshot.airborne_mission_loaded;
    airborneMissionRunning.value = snapshot.airborne_mission_running;
    missionLoaded.value = snapshot.mission_loaded;
    missionRunning.value = snapshot.mission_running;
    visionArmed.value = snapshot.vision_armed;
    currentCell.value = snapshot.current_cell;
    visitedCount.value = snapshot.visited_count;
    detectionTotals.value = snapshot.detection_totals;
    recentDetections.value = snapshot.recent_detections;
    targetUpdate.value = snapshot.target_update;
    recentSummary.value = snapshot.recent_summary;
    recentError.value = snapshot.recent_error;
    recordingError.value = snapshot.recording_error;
    hasSnapshot.value = true;
    return true;
  }

  function applyEvent(webEvent: WebEvent): void {
    const isAck = webEvent.type === 'ack';
    if (
      !hasSnapshot.value ||
      webEvent.seq <= snapshotSeq.value ||
      (!isAck && webEvent.task_id !== null && webEvent.task_id !== activeTaskId.value)
    ) {
      return;
    }

    snapshotSeq.value = webEvent.seq;
    timestampMs.value = webEvent.timestamp_ms;
    const payload = webEvent.payload;

    if (isAck) {
      if (isAckSnapshot(payload)) {
        applyAuthoritativeAck(payload);
      }
      return;
    }

    if (typeof payload.current_cell === 'string' || payload.current_cell === null) {
      currentCell.value = payload.current_cell;
    }
    const visited = payload.visited_count ?? payload.visited_cells;
    if (typeof visited === 'number' && Number.isInteger(visited)) {
      visitedCount.value = Math.max(0, visited);
    } else if (Array.isArray(visited)) {
      visitedCount.value = visited.length;
    }
    if (typeof payload.mission_running === 'boolean') {
      missionRunning.value = payload.mission_running;
    }
    if (typeof payload.mission_loaded === 'boolean') {
      missionLoaded.value = payload.mission_loaded;
    }
    if (typeof payload.vision_armed === 'boolean') {
      visionArmed.value = payload.vision_armed;
    }
    if (webEvent.event === 'detection') {
      const animal = payload.animal_name;
      const count = payload.count;
      if (typeof animal === 'string' && animal) {
        const increment = typeof count === 'number' && Number.isInteger(count)
          ? Math.max(0, count)
          : 1;
        detectionTotals.value = {
          ...detectionTotals.value,
          [animal]: (detectionTotals.value[animal] ?? 0) + increment,
        };
      }
      recentDetections.value = [...recentDetections.value, payload].slice(-100);
    }
    if (webEvent.type === 'task_summary' || webEvent.event === 'summary') {
      recentSummary.value = payload;
      missionRunning.value = false;
      airborneMissionRunning.value = false;
      if (ack.value !== null) {
        ack.value = { ...ack.value, mission_running: false };
      }
    }
  }

  function markResyncing(): void {
    commandLink.value = 'resyncing';
  }

  function applyAuthoritativeAck(nextAck: AckSnapshot): void {
    ack.value = nextAck;
    airborneTaskId.value = nextAck.task_id || null;
    airborneMissionLoaded.value = nextAck.mission_loaded;
    airborneMissionRunning.value = nextAck.mission_running;
    const matches = nextAck.task_id === activeTaskId.value;
    taskSyncState.value = matches ? 'matched' : 'mismatch';
    missionLoaded.value = matches && nextAck.mission_loaded;
    missionRunning.value = matches && nextAck.mission_running;
    visionArmed.value = matches && nextAck.vision_armed;
    if (nextAck.ok) commandLink.value = 'online';
  }

  function applyAck(response: CommandResponse): CommandResponse {
    if (response.ack) {
      applyAuthoritativeAck(response.ack);
    }
    return response;
  }

  async function runCommand(
    command: () => Promise<CommandResponse>,
  ): Promise<CommandResponse> {
    return applyAck(await command());
  }

  async function planMission(payload: PlanMissionRequest): Promise<CommandResponse> {
    const response = await api.planMission(payload);
    if (response.plan) {
      plan.value = response.plan;
      activeTaskId.value =
        typeof response.plan.task_id === 'string' ? response.plan.task_id : activeTaskId.value;
    }
    return response;
  }

  return {
    state,
    activeTaskId,
    ack,
    airborneMissionLoaded,
    airborneMissionRunning,
    airborneTaskId,
    taskSyncState,
    canPlan,
    canLoad,
    canStart,
    canStop,
    commandLink,
    currentCell,
    detectionTotals,
    recentDetections,
    recentError,
    recentSummary,
    recordingError,
    telemetryLink,
    visitedCount,
    visionArmed,
    plan,
    missionLoaded,
    missionRunning,
    snapshotSeq,
    applySnapshot,
    applyEvent,
    markResyncing,
    planMission,
    loadMission: () => runCommand(api.loadMission),
    startMission: () => runCommand(api.startMission),
    stopMission: () => runCommand(api.stopMission),
    probeLink: () => runCommand(api.probeLink),
  };
});
