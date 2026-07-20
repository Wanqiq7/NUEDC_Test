<template>
  <div class="operator-shell">
    <header class="status-bar">
      <div class="brand">
        <span class="brand-mark">NUEDC</span>
        <strong>地面任务站</strong>
      </div>
      <div class="system-status">
        <span><i :class="['status-dot', store.commandLink]" />命令链路 {{ store.commandLink }}</span>
        <span>任务 {{ store.missionRunning ? '执行中' : '待命' }}</span>
      </div>
    </header>

    <main>
      <PlanningPanel />
    </main>
  </div>
</template>

<script setup lang="ts">
import { useTelemetry } from './composables/useTelemetry';
import PlanningPanel from './panels/PlanningPanel.vue';
import { useGroundStore } from './stores/ground';

const store = useGroundStore();
useTelemetry();
</script>

<style>
:root {
  color: #e7ecef;
  background: #0c1216;
  font-family: Inter, "Noto Sans SC", "Microsoft YaHei", sans-serif;
  font-synthesis: none;
}

* {
  box-sizing: border-box;
}

html,
body,
#app {
  min-width: 1024px;
  min-height: 600px;
  height: 100%;
  margin: 0;
}

button,
input {
  font: inherit;
}

.operator-shell {
  display: grid;
  height: 100%;
  grid-template-rows: 56px minmax(0, 1fr);
  background: #0c1216;
}

.status-bar {
  display: flex;
  padding: 0 22px;
  justify-content: space-between;
  align-items: center;
  background: #182228;
  border-bottom: 1px solid #43515a;
}

.brand,
.system-status,
.system-status span {
  display: flex;
  align-items: center;
}

.brand {
  gap: 12px;
}

.brand-mark {
  padding-right: 12px;
  color: #f4c95d;
  font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
  font-weight: 800;
  border-right: 1px solid #53616a;
}

.system-status {
  gap: 24px;
  color: #b6c1c7;
  font-size: 13px;
}

.system-status span {
  gap: 7px;
}

.status-dot {
  width: 8px;
  height: 8px;
  background: #89969c;
  border-radius: 50%;
}

.status-dot.online {
  background: #45c7a0;
}

.status-dot.offline {
  background: #ef6a59;
}

main {
  min-height: 0;
  overflow: auto;
}
</style>
