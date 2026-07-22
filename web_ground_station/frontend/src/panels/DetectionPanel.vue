<template>
  <section class="detection-panel" aria-labelledby="detection-heading">
    <header><span>VISION</span><h2 id="detection-heading">实时检测</h2></header>
    <div v-if="totalEntries.length" class="detection-totals">
      <span v-for="[label, count] in totalEntries" :key="label"><strong>{{ count }}</strong>{{ label }}</span>
    </div>
    <p v-else class="empty-copy">暂无检测记录</p>
    <ol class="detection-list">
      <li v-for="(item, index) in store.recentDetections.slice(0, 4)" :key="index">
        <span>{{ detectionName(item) }}</span><strong>{{ detectionLocation(item) }}</strong>
      </li>
    </ol>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useGroundStore } from '../stores/ground';
const store = useGroundStore();
const totalEntries = computed(() => Object.entries(store.detectionTotals));
function detectionName(item: Record<string, unknown>): string {
  for (const key of ['animal_name', 'label', 'target', 'type', 'name']) if (typeof item[key] === 'string') return item[key] as string;
  return '目标';
}
function detectionLocation(item: Record<string, unknown>): string {
  for (const key of ['cell_code', 'cell', 'current_cell', 'location']) if (typeof item[key] === 'string') return item[key] as string;
  return '--';
}
</script>
