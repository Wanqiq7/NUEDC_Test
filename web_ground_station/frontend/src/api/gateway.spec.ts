import { afterEach, describe, expect, it, vi } from 'vitest';

import { ApiError, startMission } from './gateway';

describe('gateway API', () => {
  afterEach(() => vi.restoreAllMocks());

  it('maps snake_case gateway errors to ApiError fields', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      new Response(
        JSON.stringify({
          ok: false,
          error_code: 'mission_not_ready',
          message: 'mission is not ready',
        }),
        { status: 409, statusText: 'Conflict' },
      ),
    );

    await expect(startMission()).rejects.toEqual(
      new ApiError(409, 'mission_not_ready', 'mission is not ready'),
    );
  });

  it('maps a non-JSON HTTP failure to an http_error', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      new Response('<html>bad gateway</html>', {
        status: 502,
        statusText: 'Bad Gateway',
        headers: { 'Content-Type': 'text/html' },
      }),
    );

    await expect(startMission()).rejects.toMatchObject({
      status: 502,
      errorCode: 'http_error',
      message: 'Bad Gateway',
    });
  });

  it('maps an empty successful response to an invalid_response', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response('', { status: 200 }));

    await expect(startMission()).rejects.toMatchObject({
      status: 200,
      errorCode: 'invalid_response',
    });
  });

  it('maps a network rejection to a stable ApiError', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(new TypeError('Failed to fetch'));

    await expect(startMission()).rejects.toMatchObject({
      status: 0,
      errorCode: 'network_error',
    });
  });
});
