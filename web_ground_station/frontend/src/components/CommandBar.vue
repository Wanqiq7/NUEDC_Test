<template>
  <footer data-testid="command-bar" class="command-bar">
    <div class="command-context">
      <span>任务</span>
      <strong>{{ store.activeTaskId ?? '未规划' }}</strong>
      <small>{{ stateLabel }}</small>
    </div>

    <p data-testid="command-feedback" :class="['command-feedback', { failed: feedbackFailed }]" role="status">
      {{ feedback || '等待操作命令' }}
    </p>

    <div class="command-actions">
      <q-btn data-testid="load-command" icon="download" label="LOAD" :loading="pending === 'load'" :disable="!store.canLoad || pending !== null" @click="execute('load')">
        <q-tooltip>加载当前任务到机载端</q-tooltip>
      </q-btn>
      <q-btn data-testid="start-command" class="start-command" icon="play_arrow" label="START" :loading="pending === 'start'" :disable="!store.canStart || pending !== null" @click="openConfirmation('start')" />
      <q-btn data-testid="stop-command" class="stop-command" icon="stop" label="STOP" :loading="pending === 'stop'" :disable="!store.canStop || pending !== null" @click="openConfirmation('stop')" />
    </div>

    <q-dialog v-model="confirmOpen" persistent>
      <q-card class="confirm-dialog">
        <q-card-section>
          <h2>{{ selectedCommand === 'start' ? '确认启动任务' : '确认停止任务' }}</h2>
          <p>{{ selectedCommand === 'start' ? '机载端确认后才会进入运行状态。' : '停止命令将发送到当前活动任务。' }}</p>
        </q-card-section>
        <q-card-actions align="right">
          <q-btn v-close-popup flat label="取消" />
          <q-btn data-testid="confirm-command" :color="selectedCommand === 'stop' ? 'negative' : 'primary'" label="确认" @click="confirmCommand" />
        </q-card-actions>
      </q-card>
    </q-dialog>
  </footer>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { QBtn, QCard, QCardActions, QCardSection, QDialog, QTooltip } from 'quasar';
import { ApiError } from '../api/gateway';
import { useGroundStore } from '../stores/ground';

type Command = 'load' | 'start' | 'stop';
const store = useGroundStore();
const pending = ref<Command | null>(null);
const confirmOpen = ref(false);
const selectedCommand = ref<'start' | 'stop'>('start');
const feedback = ref('');
const feedbackFailed = ref(false);
const stateLabel = computed(() => store.missionRunning ? '运行中' : store.missionLoaded ? '已加载' : '未加载');

function openConfirmation(command: 'start' | 'stop'): void {
  selectedCommand.value = command;
  confirmOpen.value = true;
}

function confirmCommand(): void {
  confirmOpen.value = false;
  void execute(selectedCommand.value);
}

async function execute(command: Command): Promise<void> {
  if (pending.value) return;
  pending.value = command;
  feedback.value = '';
  feedbackFailed.value = false;
  try {
    const response = await ({ load: store.loadMission, start: store.startMission, stop: store.stopMission }[command])();
    feedback.value = response.message || response.ack?.message || '命令已确认';
  } catch (error) {
    feedbackFailed.value = true;
    feedback.value = error instanceof ApiError ? error.message : '命令执行失败';
  } finally {
    pending.value = null;
  }
}
</script>
