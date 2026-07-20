<template>
  <section class="planning-panel" aria-labelledby="planning-title">
    <header class="panel-header">
      <div>
        <p class="eyebrow">H 题 / 航线规划</p>
        <h2 id="planning-title">任务区域</h2>
      </div>
      <div class="plan-identity">
        <span>任务</span>
        <strong>{{ taskName }}</strong>
      </div>
    </header>

    <div class="planning-workspace">
      <HMissionMap
        :plan="store.plan"
        :selected-no-fly-cells="displayedNoFlyCells"
        :editable="editing"
        @toggle-cell="toggleCell"
      />

      <aside class="planning-controls" aria-label="禁飞区规划控制">
        <label class="case-input">
          <span class="section-label">案例文件</span>
          <input
            v-model.trim="casePath"
            data-testid="case-path"
            type="text"
            :disabled="planning || !store.canPlan"
            aria-label="H 题案例文件路径"
          />
        </label>
        <div class="control-section">
          <span class="section-label">禁飞区</span>
          <strong>{{ editing ? `${selectedCells.length} / 3` : `${officialNoFlyCells.length} 格` }}</strong>
          <p data-testid="selection-status" :class="{ invalid: editing && selectedCells.length === 3 && !isContiguous }">
            {{ selectionStatus }}
          </p>
        </div>

        <div v-if="metadata.terminal_cell" class="control-section route-summary">
          <span class="section-label">下降起点</span>
          <strong>{{ metadata.terminal_cell }}</strong>
          <span v-if="typeof metadata.estimated_mission_time_s === 'number'">
            预计 {{ metadata.estimated_mission_time_s.toFixed(1) }} 秒
          </span>
        </div>

        <p v-if="planningError" data-testid="planning-error" class="planning-error" role="alert">
          {{ planningError }}
        </p>
        <p v-if="planningStatus" data-testid="planning-status" class="planning-success" role="status">
          {{ planningStatus }}
        </p>

        <div class="actions">
          <button
            data-testid="edit-no-fly"
            class="secondary-action"
            type="button"
            :disabled="planning || !store.canPlan"
            @click="toggleEditing"
          >
            <q-icon :name="editing ? 'close' : 'edit_location_alt'" size="20px" aria-hidden="true" />
            {{ editing ? '取消设置' : '设置禁飞区' }}
          </button>
          <button
            data-testid="generate-plan"
            class="primary-action"
            type="button"
            :disabled="!editing || !isContiguous || planning || !store.canPlan || !casePath"
            @click="generatePlan"
          >
            <q-icon name="route" size="20px" aria-hidden="true" />
            {{ planning ? '规划中' : '生成航线' }}
          </button>
        </div>
      </aside>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { QIcon } from 'quasar';

import { ApiError } from '../api/gateway';
import HMissionMap from '../components/HMissionMap.vue';
import { useGroundStore } from '../stores/ground';

const store = useGroundStore();
const casePath = ref('shared/cases/sample_case.json');
const editing = ref(false);
const selectedCells = ref<string[]>([]);
const planning = ref(false);
const planningError = ref('');
const planningStatus = ref('');

const metadata = computed<Record<string, unknown>>(() => {
  if (typeof store.plan?.metadata_json !== 'string') return {};
  try {
    const parsed: unknown = JSON.parse(store.plan.metadata_json);
    return typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed)
      ? (parsed as Record<string, unknown>)
      : {};
  } catch {
    return {};
  }
});
const officialNoFlyCells = computed(() =>
  Array.isArray(metadata.value.no_fly_cells)
    ? metadata.value.no_fly_cells.filter((cell): cell is string => typeof cell === 'string')
    : [],
);
const displayedNoFlyCells = computed(() =>
  editing.value ? selectedCells.value : officialNoFlyCells.value,
);
const taskName = computed(() =>
  typeof store.plan?.task_id === 'string' ? store.plan.task_id : '未规划',
);
const isContiguous = computed(() => contiguousLine(selectedCells.value));
const selectionStatus = computed(() => {
  if (!editing.value) {
    return store.plan ? '当前航线由规划器生成' : '设置三格连续禁飞区后生成航线';
  }
  if (selectedCells.value.length < 3) return `请选择 3 个横向或纵向连续格子`;
  return isContiguous.value ? '三格连续，可生成航线' : '3 个格子必须横向或纵向连续';
});

function toggleEditing(): void {
  if (!store.canPlan) return;
  editing.value = !editing.value;
  selectedCells.value = [];
  planningError.value = '';
  planningStatus.value = '';
}

function toggleCell(cell: string): void {
  if (!editing.value) return;
  const index = selectedCells.value.indexOf(cell);
  if (index >= 0) {
    selectedCells.value.splice(index, 1);
  } else if (selectedCells.value.length < 3) {
    selectedCells.value.push(cell);
  }
  planningError.value = '';
  planningStatus.value = '';
}

async function generatePlan(): Promise<void> {
  if (!isContiguous.value || planning.value || !store.canPlan || !casePath.value) return;
  planning.value = true;
  planningError.value = '';
  planningStatus.value = '';
  try {
    await store.planMission({
      case_path: casePath.value,
      no_fly_cells: [...selectedCells.value],
    });
    planningStatus.value = '航线已生成';
    editing.value = false;
  } catch (error) {
    planningError.value = error instanceof ApiError ? error.message : '航线生成失败';
  } finally {
    planning.value = false;
  }
}

function contiguousLine(cells: string[]): boolean {
  if (cells.length !== 3) return false;
  const coordinates = cells.map((cell) => {
    const match = /^A([1-9])B([1-7])$/.exec(cell);
    return match ? { a: Number(match[1]), b: Number(match[2]) } : null;
  });
  if (coordinates.some((coordinate) => coordinate === null)) return false;
  const valid = coordinates as Array<{ a: number; b: number }>;
  const sameA = valid.every((coordinate) => coordinate.a === valid[0].a);
  const sameB = valid.every((coordinate) => coordinate.b === valid[0].b);
  const axis = valid.map((coordinate) => (sameA ? coordinate.b : coordinate.a)).sort((a, b) => a - b);
  return (sameA || sameB) && axis[1] === axis[0] + 1 && axis[2] === axis[1] + 1;
}
</script>

<style scoped>
.planning-panel {
  height: 100%;
  min-height: 0;
  padding: 18px 22px;
  color: #e7ecef;
  background: #11191e;
}

.panel-header {
  display: flex;
  min-height: 58px;
  justify-content: space-between;
  align-items: flex-start;
  border-bottom: 1px solid #35434b;
}

.eyebrow,
.section-label {
  margin: 0 0 4px;
  color: #8fa1ab;
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0;
  text-transform: uppercase;
}

h2 {
  margin: 0;
  font-size: 22px;
  font-weight: 650;
  letter-spacing: 0;
}

.plan-identity {
  display: grid;
  gap: 2px;
  text-align: right;
}

.plan-identity span {
  color: #8fa1ab;
  font-size: 11px;
}

.planning-workspace {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 248px;
  gap: 20px;
  padding-top: 16px;
}

.planning-controls {
  display: flex;
  min-width: 0;
  flex-direction: column;
  gap: 16px;
  padding-left: 18px;
  border-left: 1px solid #35434b;
}

.control-section {
  display: grid;
  gap: 4px;
  padding-bottom: 14px;
  border-bottom: 1px solid #2b383f;
}

.case-input {
  display: grid;
  gap: 4px;
}

.case-input input {
  width: 100%;
  min-height: 44px;
  padding: 0 10px;
  border: 1px solid #52636c;
  border-radius: 4px;
  color: #e7ecef;
  background: #0b1216;
}

.case-input input:disabled {
  opacity: 0.55;
}

.control-section strong {
  color: #f4c95d;
  font-size: 24px;
  letter-spacing: 0;
}

.control-section p,
.route-summary span {
  margin: 0;
  color: #b4c0c7;
  font-size: 13px;
  line-height: 1.45;
}

.control-section p.invalid,
.planning-error {
  color: #ff9b84;
}

.planning-error,
.planning-success {
  margin: 0;
  padding: 10px 12px;
  font-size: 13px;
  line-height: 1.4;
  border-left: 3px solid currentColor;
}

.planning-error {
  background: #2a1d1b;
}

.planning-success {
  color: #75d9bc;
  background: #162620;
}

.actions {
  display: grid;
  gap: 10px;
  margin-top: auto;
}

.actions button {
  display: inline-flex;
  min-height: 44px;
  justify-content: center;
  align-items: center;
  gap: 8px;
  padding: 0 14px;
  border-radius: 4px;
  font-weight: 700;
  letter-spacing: 0;
  cursor: pointer;
}

.secondary-action {
  color: #e3eaed;
  background: #26333a;
  border: 1px solid #687881;
}

.primary-action {
  color: #101619;
  background: #f4c95d;
  border: 1px solid #f4c95d;
}

.actions button:disabled {
  color: #7e8a90;
  background: #20292e;
  border-color: #3c484e;
  cursor: not-allowed;
}

@media (max-width: 1100px) {
  .planning-panel {
    padding: 12px 16px;
  }

  .planning-workspace {
    grid-template-columns: minmax(0, 1fr) 220px;
    gap: 14px;
  }

  .planning-controls {
    gap: 10px;
    padding-left: 14px;
  }
}
</style>
