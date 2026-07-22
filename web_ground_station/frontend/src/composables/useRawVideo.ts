import { ref, type Ref } from 'vue';

export type RawVideoState = 'idle' | 'connecting' | 'live' | 'interrupted';

interface RawVideoClient {
  state: Ref<RawVideoState>;
  open: (video: HTMLVideoElement) => Promise<void>;
  close: () => Promise<void>;
}

const MEDIA_HEALTH_CHECK_MS = 1000;
const MEDIA_STALE_AFTER_MS = 5000;
const RECONNECT_DELAYS_MS = [250, 500, 1000, 2000];

interface InboundVideoCounters {
  bytes: number;
  packets: number;
  frames: number;
}

function readInboundVideoCounters(report: RTCStatsReport): InboundVideoCounters | null {
  let found = false;
  const counters = { bytes: 0, packets: 0, frames: 0 };
  report.forEach((entry) => {
    if (entry.type !== 'inbound-rtp' || (entry.kind ?? entry.mediaType) !== 'video') return;
    found = true;
    counters.bytes += Number(entry.bytesReceived) || 0;
    counters.packets += Number(entry.packetsReceived) || 0;
    counters.frames += Number(entry.framesDecoded) || 0;
  });
  return found ? counters : null;
}

export function useRawVideo(): RawVideoClient {
  const state = ref<RawVideoState>('idle');
  let active = false;
  let generation = 0;
  let reconnectAttempt = 0;
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  let healthTimer: ReturnType<typeof setInterval> | null = null;
  let frameCallbackId: number | null = null;
  let lastMediaActivityAt = 0;
  let inboundCounters: InboundVideoCounters | null = null;
  let remoteVideoTrack: MediaStreamTrack | null = null;
  let videoElement: HTMLVideoElement | null = null;
  let peer: RTCPeerConnection | null = null;
  let sessionUrl: string | null = null;

  function clearTimers(): void {
    if (reconnectTimer !== null) clearTimeout(reconnectTimer);
    if (healthTimer !== null) clearInterval(healthTimer);
    reconnectTimer = null;
    healthTimer = null;
    if (videoElement && frameCallbackId !== null && 'cancelVideoFrameCallback' in videoElement) {
      videoElement.cancelVideoFrameCallback(frameCallbackId);
    }
    frameCallbackId = null;
    inboundCounters = null;
    remoteVideoTrack = null;
  }

  async function releaseSession(): Promise<void> {
    const url = sessionUrl;
    sessionUrl = null;
    if (url) {
      try {
        await fetch(url, { method: 'DELETE' });
      } catch {
        // A best-effort DELETE must not keep the local peer alive.
      }
    }
  }

  async function teardown(): Promise<void> {
    clearTimers();
    const oldPeer = peer;
    peer = null;
    oldPeer?.close();
    if (videoElement) videoElement.srcObject = null;
    await releaseSession();
  }

  function waitForIceGathering(connection: RTCPeerConnection): Promise<void> {
    if (connection.iceGatheringState === 'complete') return Promise.resolve();
    return new Promise((resolve) => {
      const onChange = () => {
        if (connection.iceGatheringState === 'complete') {
          connection.removeEventListener('icegatheringstatechange', onChange);
          resolve();
        }
      };
      connection.addEventListener('icegatheringstatechange', onChange);
    });
  }

  function watchFrames(video: HTMLVideoElement, ownGeneration: number): void {
    lastMediaActivityAt = Date.now();
    inboundCounters = null;
    const onFrame: VideoFrameRequestCallback = () => {
      if (!active || ownGeneration !== generation) return;
      lastMediaActivityAt = Date.now();
      reconnectAttempt = 0;
      state.value = 'live';
      frameCallbackId = video.requestVideoFrameCallback(onFrame);
    };
    if ('requestVideoFrameCallback' in video) {
      frameCallbackId = video.requestVideoFrameCallback(onFrame);
    }
    healthTimer = setInterval(() => {
      void checkMediaHealth(ownGeneration);
    }, MEDIA_HEALTH_CHECK_MS);
  }

  async function checkMediaHealth(ownGeneration: number): Promise<void> {
    const connection = peer;
    if (!active || ownGeneration !== generation || !connection) return;
    if (remoteVideoTrack?.readyState === 'ended') {
      await interrupt(ownGeneration);
      return;
    }

    try {
      const current = readInboundVideoCounters(await connection.getStats());
      if (!active || ownGeneration !== generation) return;
      if (current) {
        const previous = inboundCounters;
        inboundCounters = current;
        const hasProgress = previous
          ? current.bytes > previous.bytes
            || current.packets > previous.packets
            || current.frames > previous.frames
          : current.bytes > 0 || current.packets > 0 || current.frames > 0;
        if (hasProgress) {
          lastMediaActivityAt = Date.now();
          reconnectAttempt = 0;
          state.value = 'live';
          return;
        }
      }
    } catch {
      // Transient stats failures are not proof that the peer is dead.
    }

    if (Date.now() - lastMediaActivityAt >= MEDIA_STALE_AFTER_MS) {
      await interrupt(ownGeneration);
    }
  }

  function scheduleReconnect(): void {
    if (!active) return;
    const delay = RECONNECT_DELAYS_MS[Math.min(reconnectAttempt, RECONNECT_DELAYS_MS.length - 1)];
    reconnectAttempt += 1;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      if (active) void connect();
    }, delay);
  }

  async function interrupt(ownGeneration: number): Promise<void> {
    if (!active || ownGeneration !== generation || state.value === 'interrupted') return;
    generation += 1;
    state.value = 'interrupted';
    await teardown();
    scheduleReconnect();
  }

  async function connect(): Promise<void> {
    if (!active || !videoElement) return;
    generation += 1;
    const ownGeneration = generation;
    state.value = 'connecting';
    const connection = new RTCPeerConnection();
    peer = connection;
    connection.addTransceiver('video', { direction: 'recvonly' });
    connection.addEventListener('track', (event) => {
      if (active && ownGeneration === generation && videoElement) {
        remoteVideoTrack = event.track;
        videoElement.srcObject = event.streams[0] ?? new MediaStream([event.track]);
      }
    });
    connection.addEventListener('connectionstatechange', () => {
      if (connection.connectionState === 'failed' || connection.connectionState === 'disconnected') {
        void interrupt(ownGeneration);
      }
    });

    try {
      const offer = await connection.createOffer();
      await connection.setLocalDescription(offer);
      await waitForIceGathering(connection);
      if (!active || ownGeneration !== generation) return;
      const response = await fetch('/api/video/whep', {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: connection.localDescription?.sdp ?? '',
      });
      const location = response.headers.get('Location');
      if (!response.ok || !location?.startsWith('/api/video/whep/')) {
        throw new Error('WHEP session rejected');
      }
      const answer = await response.text();
      if (!active || ownGeneration !== generation) {
        await fetch(location, { method: 'DELETE' }).catch(() => undefined);
        return;
      }
      sessionUrl = location;
      await connection.setRemoteDescription({ type: 'answer', sdp: answer });
      watchFrames(videoElement, ownGeneration);
    } catch {
      await interrupt(ownGeneration);
    }
  }

  async function open(video: HTMLVideoElement): Promise<void> {
    if (active) await close();
    active = true;
    reconnectAttempt = 0;
    videoElement = video;
    await connect();
  }

  async function close(): Promise<void> {
    active = false;
    generation += 1;
    await teardown();
    videoElement = null;
    reconnectAttempt = 0;
    state.value = 'idle';
  }

  return { state, open, close };
}
