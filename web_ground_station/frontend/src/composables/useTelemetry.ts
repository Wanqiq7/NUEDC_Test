import { onBeforeUnmount, onMounted } from 'vue';

import { fetchSnapshot } from '../api/gateway';
import type { SnapshotEnvelope, WebEvent } from '../models/gateway';
import { useGroundStore } from '../stores/ground';

type TelemetryMessage = SnapshotEnvelope | WebEvent;

function isSnapshotEnvelope(message: TelemetryMessage): message is SnapshotEnvelope {
  return message.type === 'snapshot' && 'snapshot' in message;
}

export function useTelemetry() {
  const store = useGroundStore();
  let socket: WebSocket | null = null;
  let reconnectTimer: number | undefined;
  let stopped = false;
  let reconnectDelayMs = 250;
  let connectionId = 0;

  const connect = (): void => {
    if (stopped) return;

    const currentConnectionId = ++connectionId;
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    const pendingMessages: TelemetryMessage[] = [];
    let synchronized = false;
    const thisSocket = new WebSocket(`${scheme}://${location.host}/ws/telemetry`);
    socket = thisSocket;

    const isCurrentConnection = (): boolean =>
      !stopped && currentConnectionId === connectionId && socket === thisSocket;

    const applyMessage = (message: TelemetryMessage): void => {
      if (isSnapshotEnvelope(message)) {
        if (message.snapshot.snapshot_seq > store.snapshotSeq) {
          store.applySnapshot(message.snapshot);
        }
      } else {
        store.applyEvent(message);
      }
    };

    thisSocket.onopen = async () => {
      reconnectDelayMs = 250;
      try {
        const snapshot = await fetchSnapshot();
        if (!isCurrentConnection()) return;
        store.applySnapshot(snapshot);
        synchronized = true;
        pendingMessages.splice(0).forEach(applyMessage);
      } catch {
        if (isCurrentConnection()) thisSocket.close();
      }
    };

    thisSocket.onmessage = (event: MessageEvent<string>) => {
      if (!isCurrentConnection()) return;
      try {
        const message = JSON.parse(event.data) as TelemetryMessage;
        if (synchronized) {
          applyMessage(message);
        } else {
          pendingMessages.push(message);
        }
      } catch {
        // Ignore malformed frames and wait for the next authoritative update.
      }
    };

    thisSocket.onclose = () => {
      if (!isCurrentConnection()) return;
      reconnectTimer = window.setTimeout(connect, reconnectDelayMs);
      reconnectDelayMs = Math.min(reconnectDelayMs * 2, 5_000);
    };
  };

  onMounted(connect);
  onBeforeUnmount(() => {
    stopped = true;
    connectionId += 1;
    if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
    socket?.close();
  });

  return { connect };
}
