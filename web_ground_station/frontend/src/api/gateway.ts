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
  let response: Response;
  try {
    response = await fetch(url, {
      headers: { 'Content-Type': 'application/json' },
      ...init,
    });
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError(0, 'network_error', '网络连接失败');
  }

  let body: T & ErrorBody;
  try {
    const parsed = JSON.parse(await response.text()) as unknown;
    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
      throw new SyntaxError('response body is not an object');
    }
    body = parsed as T & ErrorBody;
  } catch {
    if (!response.ok) {
      throw new ApiError(
        response.status,
        'http_error',
        response.statusText || `HTTP ${response.status}`,
      );
    }
    throw new ApiError(response.status, 'invalid_response', '网关返回无效响应');
  }

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
