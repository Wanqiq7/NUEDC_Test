<template>
  <section class="planning-panel" aria-labelledby="planning-title">
    <header class="panel-header">
      <div>
        <p class="eyebrow"><span class="eyebrow-mark">01</span> H 题 / 航线规划</p>
        <h2 id="planning-title">任务区域 <small>MISSION FIELD</small></h2>
      </div>
      <div class="plan-identity" aria-label="当前任务">
        <span>ACTIVE TASK</span>
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
        <div class="control-heading">
          <span class="section-label">规划输入</span>
          <span class="control-index">INPUT</span>
        </div>
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
          <span class="section-label">禁飞区选择</span>
          <strong>{{ editing ? `${selectedCells.length} / 3` : `${officialNoFlyCells.length} 格` }}</strong>
          <p data-testid="selection-status" :class="{ invalid: editing && selectedCells.length === 3 && !isContiguous }">
            {{ selectionStatus }}
          </p>
        </div>

        <div v-if="metadata.terminal_cell" class="control-section route-summary">
          <span class="section-label">当前航线</span>
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
  padding: 20px 24px 16px;
  color: #e7ecef;
  background:
    linear-gradient(rgb(18 30 36 / 52%) 1px, transparent 1px),
    linear-gradient(90deg, rgb(18 30 36 / 52%) 1px, transparent 1px),
    #0d161b;
  background-size: 36px 36px;
}

.panel-header {
  display: flex;
  min-height: 68px;
  justify-content: space-between;
  align-items: flex-start;
  padding: 0 2px 14px;
  border-bottom: 1px solid #41535b;
}

.eyebrow,
.section-label {
  margin: 0 0 4px;
  color: #9bafb6;
  font: 700 10px "JetBrains Mono", "Cascadia Code", ui-monospace, monospace;
  font-weight: 700;
  letter-spacing: 0;
  text-transform: uppercase;
}

.eyebrow { display: flex; align-items: center; gap: 9px; color: #f5c451; }
.eyebrow-mark { display: inline-grid; width: 24px; height: 18px; place-items: center; color: #10181c; background: #f5c451; font-size: 10px; }

h2 {
  margin: 0;
  color: #f3f7f7;
  font-size: 26px;
  font-weight: 700;
  letter-spacing: 0;
}

h2 small { margin-left: 8px; color: #73878f; font: 700 10px "JetBrains Mono", ui-monospace, monospace; vertical-align: middle; }

.plan-identity {
  display: grid;
  gap: 5px;
  text-align: right;
}

.plan-identity strong {
  color: #f5c451;
  font: 700 13px "JetBrains Mono", "Cascadia Code", ui-monospace, monospace;
}

.plan-identity span {
  color: #6f858e;
  font: 700 10px "JetBrains Mono", ui-monospace, monospace;
}

.planning-workspace {
  display: grid;
  height: calc(100% - 84px);
  min-height: 0;
  grid-template-columns: minmax(0, 1fr) 272px;
  gap: 18px;
  padding-top: 16px;
}

.planning-workspace > .mission-map { min-height: 0; }

.planning-controls {
  display: flex;
  min-width: 0;
  flex-direction: column;
  gap: 12px;
  padding: 14px 0 0 18px;
  border-left: 1px solid #40535c;
  background: rgb(14 24 29 / 76%);
}

.control-heading { display: flex; justify-content: space-between; align-items: center; padding-bottom: 9px; border-bottom: 1px solid #2e4149; }
.control-heading .section-label { margin: 0; color: #e8eff0; }
.control-index { color: #637b84; font: 700 10px "JetBrains Mono", ui-monospace, monospace; }

.control-section {
  display: grid;
  gap: 4px;
  padding: 12px 12px 14px 0;
  border-bottom: 1px solid #2b383f;
}

.case-input {
  display: grid;
  gap: 6px;
  padding-right: 12px;
}

.case-input input {
  width: 100%;
  min-height: 44px;
  padding: 0 10px;
  border: 1px solid #526b75;
  border-radius: 4px;
  color: #e7ecef;
  background: #0b1216;
  font: 12px "JetBrains Mono", "Cascadia Code", ui-monospace, monospace;
  outline: none;
  transition: border-color 120ms ease, box-shadow 120ms ease;
}

.case-input input:focus { border-color: #f5c451; box-shadow: 0 0 0 2px rgb(245 196 81 / 18%); }

.case-input input:disabled {
  opacity: 0.55;
}

.control-section strong {
  color: #f4c95d;
  font: 700 28px "JetBrains Mono", "Cascadia Code", ui-monospace, monospace;
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
  gap: 8px;
  padding-right: 12px;
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
  transition: transform 120ms ease, filter 120ms ease;
}

.actions button:not(:disabled):hover { filter: brightness(1.08); transform: translateY(-1px); }
.actions button:focus-visible { outline: 2px solid #f5c451; outline-offset: 2px; }

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

.secondary-action { background: #1d3037; }

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
