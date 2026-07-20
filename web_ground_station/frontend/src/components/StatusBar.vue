<template>
  <header data-testid="status-bar" class="status-bar">
    <div class="link-status" aria-label="链路状态">
      <span data-testid="command-link"><i :class="['status-dot', store.commandLink]" />命令 {{ linkLabel(store.commandLink) }}</span>
      <span><i :class="['status-dot', store.telemetryLink]" />遥测 {{ linkLabel(store.telemetryLink) }}</span>
      <q-btn data-testid="refresh-link" flat round icon="refresh" :loading="probing" aria-label="刷新命令链路" @click="probeLink">
        <q-tooltip>刷新命令链路</q-tooltip>
      </q-btn>
    </div>

    <div class="live-status">
      <span>当前格 <strong>{{ store.currentCell ?? '--' }}</strong></span>
      <span>已巡检 <strong>{{ store.visitedCount }}</strong></span>
      <span>检测 <strong data-testid="detection-total">{{ detectionCount }}</strong></span>
      <span :class="['run-state', { running: store.missionRunning }]">
        {{ store.taskSyncState === 'mismatch' ? '任务不一致' : store.missionRunning ? '运行中' : '待命' }}
      </span>
      <q-btn data-testid="open-detections" flat round icon="radar" aria-label="打开检测详情" @click="detectionsOpen = true">
        <q-tooltip>检测详情</q-tooltip>
      </q-btn>
      <q-btn data-testid="open-details" flat round icon="info" aria-label="打开任务详情" @click="detailsOpen = true">
        <q-tooltip>任务详情</q-tooltip>
      </q-btn>
    </div>

    <q-dialog v-model="detailsOpen">
      <q-card v-if="detailsOpen" class="details-dialog">
        <q-card-section class="dialog-title">
          <strong>任务详情</strong>
          <q-btn v-close-popup flat round icon="close" aria-label="关闭任务详情" />
        </q-card-section>
        <q-separator dark />
        <q-card-section class="detail-grid">
          <span>活动任务</span><strong>{{ store.activeTaskId ?? '未规划' }}</strong>
          <span>机载任务</span><strong>{{ store.airborneTaskId ?? '无' }}</strong>
          <span>同步状态</span><strong>{{ syncLabel }}</strong>
          <span>命令链路</span><strong>{{ linkLabel(store.commandLink) }}</strong>
          <span>遥测链路</span><strong>{{ linkLabel(store.telemetryLink) }}</strong>
          <span>任务状态</span><strong>{{ store.missionRunning ? '运行中' : store.missionLoaded ? '已加载' : '未加载' }}</strong>
          <span>视觉检测</span><strong>{{ store.visionArmed ? '已启用' : '未启用' }}</strong>
          <span>最近错误</span><strong class="error-copy">{{ readable(store.recentError) }}</strong>
          <span>任务摘要</span><strong>{{ readable(store.recentSummary) }}</strong>
        </q-card-section>
      </q-card>
    </q-dialog>

    <q-dialog v-model="detectionsOpen">
      <q-card v-if="detectionsOpen" data-testid="detection-dialog-content" class="details-dialog detection-dialog">
        <q-card-section class="dialog-title">
          <strong>检测详情</strong>
          <q-btn v-close-popup flat round icon="close" aria-label="关闭检测详情" />
        </q-card-section>
        <q-separator dark />
        <q-card-section>
          <div v-if="detectionEntries.length" class="dialog-detection-totals" aria-label="检测总计">
            <span v-for="[label, count] in detectionEntries" :key="label">
              <strong>{{ count }}</strong>{{ label }}
            </span>
          </div>
          <p v-else class="empty-copy">暂无检测记录</p>
          <ol class="dialog-detection-list" aria-label="最近检测">
            <li v-for="(item, index) in store.recentDetections" :key="index">
              <span>{{ detectionName(item) }}</span>
              <strong>{{ detectionLocation(item) }}</strong>
            </li>
          </ol>
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
const detectionsOpen = ref(false);
const probing = ref(false);
const syncLabel = computed(() => ({ unconfirmed: '未确认', matched: '一致', mismatch: '任务不一致' })[store.taskSyncState]);
const detectionCount = computed(() => Object.values(store.detectionTotals).reduce((sum, count) => sum + count, 0));
const detectionEntries = computed(() => Object.entries(store.detectionTotals));

async function probeLink(): Promise<void> {
  if (probing.value) return;
  probing.value = true;
  try {
    await store.probeLink();
  } finally {
    probing.value = false;
  }
}

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

function detectionName(item: Record<string, unknown>): string {
  for (const key of ['animal_name', 'label', 'target', 'type', 'name']) {
    if (typeof item[key] === 'string') return item[key] as string;
  }
  return '目标';
}

function detectionLocation(item: Record<string, unknown>): string {
  for (const key of ['cell_code', 'cell', 'current_cell', 'location']) {
    if (typeof item[key] === 'string') return item[key] as string;
  }
  return '--';
}
</script>
