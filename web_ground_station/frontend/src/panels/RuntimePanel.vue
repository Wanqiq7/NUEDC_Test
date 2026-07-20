<template>
  <section class="runtime-panel" aria-labelledby="runtime-heading">
    <header><span>MISSION</span><h2 id="runtime-heading">运行状态</h2></header>
    <dl class="metric-list">
      <div><dt>当前格</dt><dd>{{ store.currentCell ?? '--' }}</dd></div>
      <div><dt>已巡检</dt><dd>{{ store.visitedCount }}</dd></div>
      <div><dt>任务状态</dt><dd>{{ store.missionRunning ? '运行中' : store.missionLoaded ? '已加载' : '未加载' }}</dd></div>
      <div><dt>视觉检测</dt><dd>{{ store.visionArmed ? '已启用' : '未启用' }}</dd></div>
    </dl>
    <div class="text-block"><span>最近错误</span><p class="error-copy">{{ readable(store.recentError) }}</p></div>
    <div class="text-block"><span>任务摘要</span><p>{{ readable(store.recentSummary) }}</p></div>
  </section>
</template>

<script setup lang="ts">
import { useGroundStore } from '../stores/ground';
const store = useGroundStore();
function readable(value: Record<string, unknown> | null): string {
  if (!value) return '无';
  for (const key of ['message', 'summary', 'error', 'detail']) if (typeof value[key] === 'string') return value[key] as string;
  return JSON.stringify(value);
}
</script>
