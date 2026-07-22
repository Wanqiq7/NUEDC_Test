// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

class FakePeerConnection extends EventTarget {
  static instances: FakePeerConnection[] = [];
  iceGatheringState: RTCIceGatheringState = 'new';
  connectionState: RTCPeerConnectionState = 'new';
  localDescription: RTCSessionDescriptionInit | null = null;
  remoteDescription: RTCSessionDescriptionInit | null = null;
  closed = false;
  addTransceiver = vi.fn();
  inboundBytes = 0;
  inboundPackets = 0;

  constructor() {
    super();
    FakePeerConnection.instances.push(this);
  }

  async createOffer(): Promise<RTCSessionDescriptionInit> {
    return { type: 'offer', sdp: 'browser-offer' };
  }

  async setLocalDescription(description: RTCSessionDescriptionInit): Promise<void> {
    this.localDescription = description;
    queueMicrotask(() => {
      this.iceGatheringState = 'complete';
      this.dispatchEvent(new Event('icegatheringstatechange'));
    });
  }

  async setRemoteDescription(description: RTCSessionDescriptionInit): Promise<void> {
    this.remoteDescription = description;
  }

  getStats = vi.fn(async () => new Map([
    ['video-inbound', {
      id: 'video-inbound',
      type: 'inbound-rtp',
      kind: 'video',
      bytesReceived: this.inboundBytes,
      packetsReceived: this.inboundPackets,
    }],
  ]) as unknown as RTCStatsReport);

  close(): void {
    this.closed = true;
  }
}

function fakeVideo() {
  let frameCallback: VideoFrameRequestCallback | null = null;
  return {
    srcObject: null as MediaStream | null,
    requestVideoFrameCallback: vi.fn((callback: VideoFrameRequestCallback) => {
      frameCallback = callback;
      return 1;
    }),
    cancelVideoFrameCallback: vi.fn(),
    emitFrame(now = performance.now()) {
      frameCallback?.(now, {} as VideoFrameCallbackMetadata);
    },
  };
}

describe('useRawVideo', () => {
  beforeEach(() => {
    vi.clearAllTimers();
    vi.resetModules();
    vi.useFakeTimers();
    FakePeerConnection.instances = [];
    vi.stubGlobal('RTCPeerConnection', FakePeerConnection);
    vi.stubGlobal('fetch', vi.fn(async (_input: RequestInfo | URL, init?: RequestInit) => {
      if (init?.method === 'DELETE') return new Response(null, { status: 204 });
      return new Response('media-answer', {
        status: 201,
        headers: { 'Content-Type': 'application/sdp', Location: '/api/video/whep/session-1' },
      });
    }));
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it('posts a complete ICE offer and installs the video-only answer', async () => {
    const { useRawVideo } = await import('./useRawVideo');
    const video = fakeVideo();
    const client = useRawVideo();

    await client.open(video as unknown as HTMLVideoElement);

    const peer = FakePeerConnection.instances[0];
    expect(peer.addTransceiver).toHaveBeenCalledWith('video', { direction: 'recvonly' });
    expect(fetch).toHaveBeenCalledWith('/api/video/whep', expect.objectContaining({
      method: 'POST',
      body: 'browser-offer',
      headers: { 'Content-Type': 'application/sdp' },
    }));
    expect(peer.remoteDescription).toEqual({ type: 'answer', sdp: 'media-answer' });
    expect(client.state.value).toBe('connecting');
  });

  it('releases the WHEP resource and peer when closed', async () => {
    const { useRawVideo } = await import('./useRawVideo');
    const video = fakeVideo();
    const client = useRawVideo();
    await client.open(video as unknown as HTMLVideoElement);

    await client.close();

    expect(fetch).toHaveBeenLastCalledWith('/api/video/whep/session-1', { method: 'DELETE' });
    expect(FakePeerConnection.instances[0].closed).toBe(true);
    expect(video.srcObject).toBeNull();
    expect(client.state.value).toBe('idle');
  });

  it('drops a session with no media progress and reconnects with bounded backoff', async () => {
    const { useRawVideo } = await import('./useRawVideo');
    const video = fakeVideo();
    const client = useRawVideo();
    await client.open(video as unknown as HTMLVideoElement);
    const firstPeer = FakePeerConnection.instances[0];
    firstPeer.dispatchEvent(Object.assign(new Event('track'), { streams: [{} as MediaStream] }));
    video.emitFrame();
    expect(client.state.value).toBe('live');

    await vi.advanceTimersByTimeAsync(5000);
    expect(client.state.value).toBe('interrupted');
    expect(firstPeer.closed).toBe(true);

    await vi.advanceTimersByTimeAsync(250);
    await Promise.resolve();
    expect(FakePeerConnection.instances).toHaveLength(2);
    expect(fetch).toHaveBeenCalledWith('/api/video/whep/session-1', { method: 'DELETE' });
  });

  it('keeps a healthy RTP session when requestVideoFrameCallback is temporarily silent', async () => {
    const { useRawVideo } = await import('./useRawVideo');
    const video = fakeVideo();
    const client = useRawVideo();
    await client.open(video as unknown as HTMLVideoElement);
    const peer = FakePeerConnection.instances[0];
    peer.connectionState = 'connected';
    peer.dispatchEvent(new Event('connectionstatechange'));
    peer.dispatchEvent(Object.assign(new Event('track'), {
      streams: [{} as MediaStream],
      track: { readyState: 'live' } as MediaStreamTrack,
    }));

    for (let second = 1; second <= 7; second += 1) {
      peer.inboundBytes = second * 4096;
      peer.inboundPackets = second * 24;
      await vi.advanceTimersByTimeAsync(1000);
    }

    expect(client.state.value).toBe('live');
    expect(peer.closed).toBe(false);
    expect(FakePeerConnection.instances).toHaveLength(1);
    expect(fetch).not.toHaveBeenCalledWith('/api/video/whep/session-1', { method: 'DELETE' });
  });
});
