import { defineConfig } from 'playwright/test';

export default defineConfig({
  testDir: '../tests/e2e',
  fullyParallel: false,
  workers: 1,
  timeout: 45_000,
  expect: { timeout: 10_000 },
  use: {
    launchOptions: {
      executablePath: process.env.NUEDC_CHROME_BIN ?? '/usr/bin/google-chrome',
    },
    viewport: { width: 1366, height: 768 },
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  outputDir: '../tests/e2e/output',
  reporter: [['list']],
});
