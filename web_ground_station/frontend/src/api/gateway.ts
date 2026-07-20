import type {
  CommandResponse,
  GroundSnapshot,
  PlanMissionRequest,
} from '../models/gateway';

interface ErrorBody {
  ok?: boolean;
  message?: string;
  error_code?: string | null;
}

export class ApiError extends Error {
  constructor(
    public readonly status: number,
    public readonly errorCode: string,
    message: string,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, {
    headers: { 'Content-Type': 'application/json' },
    ...init,
  });
  const body = (await response.json()) as T & ErrorBody;

  if (!response.ok || body.ok === false) {
    throw new ApiError(
      response.status,
      body.error_code ?? 'request_failed',
      body.message ?? response.statusText,
    );
  }

  return body;
}

export const fetchSnapshot = (): Promise<GroundSnapshot> =>
  request<GroundSnapshot>('/api/snapshot');

export const planMission = (payload: PlanMissionRequest): Promise<CommandResponse> =>
  request<CommandResponse>('/api/mission/plan', {
    method: 'POST',
    body: JSON.stringify(payload),
  });

export const loadMission = (): Promise<CommandResponse> =>
  request<CommandResponse>('/api/mission/load', { method: 'POST' });

export const startMission = (): Promise<CommandResponse> =>
  request<CommandResponse>('/api/mission/start', { method: 'POST' });

export const stopMission = (): Promise<CommandResponse> =>
  request<CommandResponse>('/api/mission/stop', { method: 'POST' });

export const probeLink = (): Promise<CommandResponse> =>
  request<CommandResponse>('/api/link/probe', { method: 'POST' });
