<template>
  <header data-testid="status-bar" class="status-bar">
    <div class="brand" aria-label="全国大学生电子设计竞赛地面任务站">
      <span class="brand-mark">NUEDC</span>
      <strong>任务主控台</strong>
    </div>

    <div class="link-status" aria-label="链路状态">
      <span><i :class="['status-dot', store.commandLink]" />命令 {{ linkLabel(store.commandLink) }}</span>
      <span><i :class="['status-dot', store.telemetryLink]" />遥测 {{ linkLabel(store.telemetryLink) }}</span>
    </div>

    <div class="live-status">
      <span>当前格 <strong>{{ store.currentCell ?? '--' }}</strong></span>
      <span>已巡检 <strong>{{ store.visitedCount }}</strong></span>
      <span>检测 <strong>{{ detectionCount }}</strong></span>
      <span :class="['run-state', { running: store.missionRunning }]">
        {{ store.missionRunning ? '运行中' : '待命' }}
      </span>
      <q-btn data-testid="open-details" flat round icon="info" aria-label="打开任务详情" @click="detailsOpen = true">
        <q-tooltip>任务详情</q-tooltip>
      </q-btn>
    </div>

    <q-dialog v-model="detailsOpen">
      <q-card class="details-dialog">
        <q-card-section class="dialog-title">
          <strong>任务详情</strong>
          <q-btn v-close-popup flat round icon="close" aria-label="关闭任务详情" />
        </q-card-section>
        <q-separator dark />
        <q-card-section class="detail-grid">
          <span>活动任务</span><strong>{{ store.activeTaskId ?? '未规划' }}</strong>
          <span>命令链路</span><strong>{{ linkLabel(store.commandLink) }}</strong>
          <span>遥测链路</span><strong>{{ linkLabel(store.telemetryLink) }}</strong>
          <span>任务状态</span><strong>{{ store.missionRunning ? '运行中' : store.missionLoaded ? '已加载' : '未加载' }}</strong>
          <span>视觉检测</span><strong>{{ store.visionArmed ? '已启用' : '未启用' }}</strong>
          <span>最近错误</span><strong class="error-copy">{{ readable(store.recentError) }}</strong>
          <span>任务摘要</span><strong>{{ readable(store.recentSummary) }}</strong>
        </q-card-section>
      </q-card>
    </q-dialog>
  </header>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { QBtn, QCard, QCardSection, QDialog, QSeparator, QTooltip } from 'quasar';
import { useGroundStore } from '../stores/ground';

const store = useGroundStore();
const detailsOpen = ref(false);
const detectionCount = computed(() => Object.values(store.detectionTotals).reduce((sum, count) => sum + count, 0));

function linkLabel(link: string): string {
  return ({ online: '在线', offline: '离线', resyncing: '同步中', unknown: '未知' } as Record<string, string>)[link] ?? link;
}

function readable(value: Record<string, unknown> | null): string {
  if (!value) return '无';
  for (const key of ['message', 'summary', 'error', 'detail']) {
    if (typeof value[key] === 'string') return value[key] as string;
  }
  return JSON.stringify(value);
}
</script>
