import { spawn, type ChildProcess } from 'node:child_process';
import { createServer } from 'node:net';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';

import { expect, test as base, type TestInfo } from 'playwright/test';

type ManagedProcess = {
  child: ChildProcess;
  label: string;
  output: string[];
  startupError?: Error;
};

type GroundStationFixtures = {
  groundStation: GroundStationControl;
  baseURL: string;
  restartGateway: () => Promise<void>;
};

type GroundStationControl = {
  baseURL: string;
  restartGateway: () => Promise<void>;
};

const repoRoot = resolve(__dirname, '../../..');
const webRoot = resolve(repoRoot, 'web_ground_station');

async function selectEphemeralPort(): Promise<number> {
  return await new Promise((resolvePort, reject) => {
    const server = createServer();
    server.unref();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      if (!address || typeof address === 'string') {
        server.close();
        reject(new Error('failed to reserve a local port'));
        return;
      }
      const port = address.port;
      server.close((error) => error ? reject(error) : resolvePort(port));
    });
  });
}

function startProcess(label: string, command: string, args: string[], env: NodeJS.ProcessEnv): ManagedProcess {
  const child = spawn(command, args, {
    cwd: webRoot,
    env,
    detached: process.platform !== 'win32',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const managed: ManagedProcess = { child, label, output: [] };
  const capture = (chunk: Buffer) => {
    managed.output.push(chunk.toString());
    if (managed.output.length > 400) managed.output.shift();
  };
  child.stdout?.on('data', capture);
  child.stderr?.on('data', capture);
  child.once('error', (error) => {
    managed.startupError = error;
    managed.output.push(`${error.name}: ${error.message}\n`);
  });
  return managed;
}

async function stopProcess(managed: ManagedProcess | null): Promise<void> {
  if (!managed || managed.child.exitCode !== null || managed.child.signalCode !== null) return;
  const pid = managed.child.pid;
  if (!pid) return;
  const signal = (name: NodeJS.Signals) => {
    try {
      process.kill(process.platform === 'win32' ? pid : -pid, name);
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'ESRCH') throw error;
    }
  };
  signal('SIGTERM');
  if (await waitForExit(managed.child, 2_000)) return;
  signal('SIGKILL');
  if (!await waitForExit(managed.child, 2_000)) {
    throw new Error(`${managed.label} did not exit after SIGKILL`);
  }
}

async function waitForExit(child: ChildProcess, timeoutMs: number): Promise<boolean> {
  if (child.exitCode !== null || child.signalCode !== null) return true;
  return await new Promise((resolveExit) => {
    const timeout = setTimeout(() => {
      child.off('exit', onExit);
      resolveExit(false);
    }, timeoutMs);
    const onExit = () => {
      clearTimeout(timeout);
      resolveExit(true);
    };
    child.once('exit', onExit);
  });
}

async function waitForHealth(url: string, process: ManagedProcess): Promise<void> {
  const deadline = Date.now() + 15_000;
  while (Date.now() < deadline) {
    if (process.startupError) {
      throw new Error(`${process.label} failed to start\n${process.output.join('')}`);
    }
    if (process.child.exitCode !== null) {
      throw new Error(`${process.label} exited with ${process.child.exitCode}\n${process.output.join('')}`);
    }
    try {
      const response = await fetch(url);
      if (response.ok) return;
    } catch {
      // The server may still be starting.
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 100));
  }
  throw new Error(`${process.label} did not become healthy\n${process.output.join('')}`);
}

async function attachLogs(testInfo: TestInfo, processes: ManagedProcess[]): Promise<void> {
  if (testInfo.status === testInfo.expectedStatus) return;
  for (const process of processes) {
    await testInfo.attach(`${process.label}.log`, {
      body: Buffer.from(process.output.join(''), 'utf8'),
      contentType: 'text/plain',
    });
  }
}

export const test = base.extend<GroundStationFixtures>({
  groundStation: async ({}, use, testInfo) => {
    const processes: ManagedProcess[] = [];
    let runtimeDir: string | null = null;
    let gateway: ManagedProcess | null = null;

    try {
      const telemetryPort = await selectEphemeralPort();
      const commandPort = await selectEphemeralPort();
      const webPort = await selectEphemeralPort();
      runtimeDir = await mkdtemp(resolve(tmpdir(), 'nuedc-web-e2e-'));
      const env = {
        ...process.env,
        PYTHONPATH: resolve(webRoot, 'gateway'),
        NUEDC_AIRBORNE_HOST: '127.0.0.1',
        NUEDC_TELEMETRY_PORT: String(telemetryPort),
        NUEDC_COMMAND_PORT: String(commandPort),
        NUEDC_WEB_HOST: '127.0.0.1',
        NUEDC_WEB_PORT: String(webPort),
        NUEDC_RUNTIME_DIR: runtimeDir,
        NUEDC_PLANNER_CLI: resolve(repoRoot, 'build/shared/cpp/h_route_planner_cli'),
        UV_CACHE_DIR: resolve(runtimeDir, 'uv-cache'),
      };
      const mock = startProcess('mock-airborne', 'uv', [
        'run', 'python', resolve(repoRoot, 'scripts/mock_airborne.py'),
        '--telemetry-port', String(telemetryPort),
        '--command-port', String(commandPort),
        '--runtime-path', resolve(runtimeDir, 'mock-plan.json'),
        '--interval-s', '0.02',
        '--detection-every', '3',
        '--pub-warmup-s', '0.1',
      ], env);
      processes.push(mock);

      const startGateway = async () => {
        gateway = startProcess('gateway', 'uv', [
          'run', 'uvicorn', 'nuedc_web_gateway.app:create_app', '--factory',
          '--host', '127.0.0.1', '--port', String(webPort),
        ], env);
        processes.push(gateway);
        await waitForHealth(`http://127.0.0.1:${webPort}/api/health`, gateway);
      };
      await startGateway();

      await use({
        baseURL: `http://127.0.0.1:${webPort}`,
        restartGateway: async () => {
          await stopProcess(gateway);
          await startGateway();
        },
      });
    } finally {
      try {
        const cleanupResults = await Promise.allSettled(
          [...processes].reverse().map(stopProcess),
        );
        await attachLogs(testInfo, processes);
        const cleanupErrors = cleanupResults
          .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
          .map((result) => result.reason);
        if (cleanupErrors.length) throw new AggregateError(cleanupErrors, 'process cleanup failed');
      } finally {
        if (runtimeDir !== null) await rm(runtimeDir, { recursive: true, force: true });
      }
    }
  },

  baseURL: async ({ groundStation }, use) => {
    await use(groundStation.baseURL);
  },

  restartGateway: async ({ groundStation }, use) => {
    await use(groundStation.restartGateway);
  },
});

export { expect };
