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

export const useGroundStore = defineStore('ground', () => {
  const snapshotSeq = ref(0);
  const timestampMs = ref(0);
  const activeTaskId = ref<string | null>(null);
  const plan = ref<DynamicPayload | null>(null);
  const commandLink = ref('unknown');
  const telemetryLink = ref('unknown');
  const pidLink = ref('unknown');
  const ack = ref<AckSnapshot | null>(null);
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

  function applySnapshot(snapshot: GroundSnapshot): void {
    snapshotSeq.value = snapshot.snapshot_seq;
    timestampMs.value = snapshot.timestamp_ms;
    activeTaskId.value = snapshot.active_task_id;
    plan.value = snapshot.plan;
    commandLink.value = snapshot.command_link;
    telemetryLink.value = snapshot.telemetry_link;
    pidLink.value = snapshot.pid_link;
    ack.value = snapshot.ack;
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
  }

  function applyEvent(webEvent: WebEvent): void {
    if (
      !hasSnapshot.value ||
      webEvent.seq <= snapshotSeq.value ||
      (webEvent.task_id !== null && webEvent.task_id !== activeTaskId.value)
    ) {
      return;
    }

    snapshotSeq.value = webEvent.seq;
    timestampMs.value = webEvent.timestamp_ms;
    const payload = webEvent.payload;

    if (typeof payload.current_cell === 'string' || payload.current_cell === null) {
      currentCell.value = payload.current_cell;
    }
    if (typeof payload.visited_count === 'number') {
      visitedCount.value = payload.visited_count;
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
  }

  function applyAck(response: CommandResponse): CommandResponse {
    if (response.ack) {
      ack.value = response.ack;
      missionLoaded.value = response.ack.mission_loaded;
      missionRunning.value = response.ack.mission_running;
      visionArmed.value = response.ack.vision_armed;
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
    commandLink,
    currentCell,
    missionLoaded,
    missionRunning,
    snapshotSeq,
    applySnapshot,
    applyEvent,
    planMission,
    loadMission: () => runCommand(api.loadMission),
    startMission: () => runCommand(api.startMission),
    stopMission: () => runCommand(api.stopMission),
    probeLink: () => runCommand(api.probeLink),
  };
});
