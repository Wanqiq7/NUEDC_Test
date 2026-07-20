<template>
  <div data-testid="console-shell" class="console-shell">
    <StatusBar />
    <main class="console-main">
      <section data-testid="map-region" class="map-region" aria-label="任务规划地图">
        <PlanningPanel />
      </section>
      <aside data-testid="console-rail" class="console-rail" aria-label="运行和检测详情">
        <RuntimePanel />
        <DetectionPanel />
      </aside>
    </main>
    <CommandBar />
  </div>
</template>

<script setup lang="ts">
import './styles/app.scss';
import CommandBar from './components/CommandBar.vue';
import StatusBar from './components/StatusBar.vue';
import { useTelemetry } from './composables/useTelemetry';
import DetectionPanel from './panels/DetectionPanel.vue';
import PlanningPanel from './panels/PlanningPanel.vue';
import RuntimePanel from './panels/RuntimePanel.vue';
useTelemetry();
</script>
