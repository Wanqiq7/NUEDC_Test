export type DynamicPayload = Record<string, unknown>;

export interface AckSnapshot {
  ok: boolean;
  message: string;
  task_id: string;
  mission_loaded: boolean;
  mission_running: boolean;
  last_accepted_sequence: number;
  vision_armed: boolean;
}

export interface GroundSnapshot {
  snapshot_seq: number;
  timestamp_ms: number;
  active_task_id: string | null;
  plan: DynamicPayload | null;
  command_link: string;
  telemetry_link: string;
  pid_link: string;
  ack: AckSnapshot | null;
  mission_loaded: boolean;
  mission_running: boolean;
  vision_armed: boolean;
  current_cell: string | null;
  visited_count: number;
  detection_totals: Record<string, number>;
  recent_detections: DynamicPayload[];
  target_update: DynamicPayload | null;
  recent_summary: DynamicPayload | null;
  recent_error: DynamicPayload | null;
  recording_error: string;
}

export interface WebEvent {
  schema: 'nuedc.web.v1';
  type: string;
  seq: number;
  timestamp_ms: number;
  task_id: string | null;
  event: string | null;
  payload: DynamicPayload;
}

export interface SnapshotEnvelope {
  type: 'snapshot';
  snapshot: GroundSnapshot;
}

export interface CommandResponse {
  ok: boolean;
  message?: string;
  error_code?: string | null;
  ack?: AckSnapshot | null;
  plan?: DynamicPayload;
}

export interface PlanMissionRequest {
  case_path: string;
  no_fly_cells: string[];
}
