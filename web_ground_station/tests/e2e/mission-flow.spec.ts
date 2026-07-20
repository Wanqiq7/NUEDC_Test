import type { Page } from 'playwright/test';

import { expect, test } from './fixtures';

async function planAndLoad(page: Page): Promise<void> {
  await page.getByTestId('edit-no-fly').click();
  await page.getByTestId('cell-A2B2').click();
  await page.getByTestId('cell-A2B3').click();
  await page.getByTestId('cell-A2B4').click();
  await page.getByTestId('generate-plan').click();
  await expect(page.getByTestId('route')).toBeVisible();
  await page.getByTestId('load-command').click();
  await expect(page.getByText('已加载', { exact: true }).first()).toBeVisible();
}

test('plans, loads, starts, observes and completes one H mission', async ({ page, baseURL }) => {
  await page.goto(baseURL);
  await expect(page.getByTestId('command-link')).toContainText('在线');
  await planAndLoad(page);

  await page.getByTestId('start-command').click();
  await page.getByTestId('confirm-command').click();
  await expect(page.getByText('运行中', { exact: true }).first()).toBeVisible();
  await expect(page.getByTestId('detection-total')).not.toHaveText('0');
  await expect(page.getByTestId('mission-summary')).toHaveText('任务完成', { timeout: 30_000 });

  await page.reload();
  await expect(page.getByTestId('mission-summary')).toHaveText('任务完成');
  await expect(page.getByTestId('detection-total')).not.toHaveText('0');
});

test('resynchronizes authoritative state after Gateway restart', async ({
  page,
  baseURL,
  restartGateway,
}) => {
  await page.goto(baseURL);
  await expect(page.getByTestId('command-link')).toContainText('在线');
  await planAndLoad(page);

  const restart = restartGateway();
  await expect(page.getByTestId('command-link')).toContainText('同步中');
  await restart;
  await expect(page.getByTestId('command-link')).toContainText('在线', { timeout: 10_000 });
  await expect(page.getByTestId('start-command')).toBeEnabled();
});
