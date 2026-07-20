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
});
