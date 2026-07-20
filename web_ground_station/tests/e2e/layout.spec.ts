import type { Locator, Page } from 'playwright/test';

import { expect, test } from './fixtures';

const viewports = [
  { width: 1920, height: 1080 },
  { width: 1366, height: 768 },
  { width: 1024, height: 600 },
] as const;

async function overlap(left: Locator, right: Locator): Promise<boolean> {
  const [a, b] = await Promise.all([left.boundingBox(), right.boundingBox()]);
  if (!a || !b) throw new Error('required layout element has no bounding box');
  return Math.min(a.x + a.width, b.x + b.width) > Math.max(a.x, b.x)
    && Math.min(a.y + a.height, b.y + b.height) > Math.max(a.y, b.y);
}

async function assertNoOverlap(page: Page, testIds: string[]): Promise<void> {
  const collisions: string[] = [];
  for (let left = 0; left < testIds.length; left += 1) {
    for (let right = left + 1; right < testIds.length; right += 1) {
      if (await overlap(page.getByTestId(testIds[left]), page.getByTestId(testIds[right]))) {
        collisions.push(`${testIds[left]}:${testIds[right]}`);
      }
    }
  }
  expect(collisions).toEqual([]);
}

test('keeps the complete operator surface usable at every supported viewport', async ({
  page,
  baseURL,
}) => {
  const localOrigin = new URL(baseURL).origin;
  const externalRequests = new Set<string>();
  page.on('request', (request) => {
    const url = new URL(request.url());
    if (url.origin !== localOrigin) externalRequests.add(request.url());
  });

  for (const viewport of viewports) {
    await page.setViewportSize(viewport);
    await page.goto(baseURL);
    await expect(page.getByTestId('status-bar')).toBeInViewport();
    await expect(page.getByTestId('mission-map')).toBeInViewport();
    await expect(page.getByTestId('command-bar')).toBeInViewport();
    await assertNoOverlap(page, ['status-bar', 'mission-map', 'command-bar']);
    await expect(page.getByTestId('load-command')).toHaveCSS('min-height', /4[4-9]px|5\dpx/);
  }

  expect([...externalRequests]).toEqual([]);
});
