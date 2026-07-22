<template>
  <q-dialog :model-value="modelValue" maximized @update:model-value="setOpen">
    <div v-if="modelValue" data-testid="raw-video-dialog" class="raw-video-dialog" aria-label="原始图传">
      <header class="raw-video-toolbar">
        <strong>原始图传</strong>
        <span :class="['raw-video-state', state]">{{ stateLabel }}</span>
        <q-btn flat round icon="close" aria-label="关闭原始图传" @click="setOpen(false)">
          <q-tooltip>关闭原始图传</q-tooltip>
        </q-btn>
      </header>
      <div class="raw-video-viewport">
        <video ref="video" data-testid="raw-video" autoplay muted playsinline />
      </div>
    </div>
  </q-dialog>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue';
import { QBtn, QDialog, QTooltip } from 'quasar';
import { useRawVideo } from '../composables/useRawVideo';

const props = defineProps<{ modelValue: boolean }>();
const emit = defineEmits<{ 'update:modelValue': [value: boolean] }>();
const video = ref<HTMLVideoElement | null>(null);
const { state, open, close } = useRawVideo();
const stateLabel = computed(() => ({
  idle: '已关闭',
  connecting: '正在连接',
  live: '实时',
  interrupted: '信号中断',
})[state.value]);

function setOpen(value: boolean): void {
  emit('update:modelValue', value);
}

watch(
  () => props.modelValue,
  async (visible) => {
    if (!visible) {
      await close();
      return;
    }
    await nextTick();
    if (video.value) await open(video.value);
  },
  { immediate: true },
);

onBeforeUnmount(() => {
  void close();
});
</script>
