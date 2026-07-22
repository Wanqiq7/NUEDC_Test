import { expect, test } from './fixtures';

const viewports = [
  { width: 1024, height: 600 },
  { width: 1366, height: 768 },
  { width: 1920, height: 1080 },
] as const;

test('opens and closes the complete raw-video surface at every viewport', async ({
  page,
  baseURL,
}, testInfo) => {
  for (const viewport of viewports) {
    await page.setViewportSize(viewport);
    await page.goto(baseURL);
    const action = page.getByTestId('open-raw-video');
    await expect(action).toBeEnabled();
    await action.click();

    const dialog = page.getByTestId('raw-video-dialog');
    const video = page.getByTestId('raw-video');
    await expect(dialog).toBeVisible();
    await expect(dialog).toHaveCSS('width', `${viewport.width}px`);
    await expect(dialog).toHaveCSS('height', `${viewport.height}px`);
    await expect.poll(async () => {
      const box = await dialog.boundingBox();
      return box && {
        x: Math.round(box.x),
        y: Math.round(box.y),
        width: Math.round(box.width),
        height: Math.round(box.height),
      };
    }).toEqual({ x: 0, y: 0, width: viewport.width, height: viewport.height });
    await expect(video).toHaveCSS('object-fit', 'contain');
    await expect(video).toHaveJSProperty('muted', true);
    await expect(video).toHaveAttribute('playsinline', '');
    await page.screenshot({
      path: testInfo.outputPath(`raw-video-${viewport.width}x${viewport.height}.png`),
      fullPage: true,
    });

    await page.getByRole('button', { name: '关闭原始图传' }).click();
    await expect(dialog).toBeHidden();
  }
});
