<template>
  <section data-testid="mission-map" class="mission-map" aria-label="H题任务地图">
    <div class="map-toolbar" aria-label="地图视图控制">
      <button
        v-for="control in controls"
        :key="control.label"
        class="map-control"
        type="button"
        :aria-label="control.label"
        :title="control.label"
        @click="control.action"
      >
        <q-icon :name="control.icon" size="22px" aria-hidden="true" />
      </button>
    </div>

    <svg
      class="map-canvas"
      :viewBox="viewBox"
      preserveAspectRatio="xMidYMid meet"
      role="img"
      aria-label="9乘7任务格网与规划航线"
    >
      <rect class="field" x="25" y="25" width="450" height="350" />

      <g class="axis-labels" aria-hidden="true">
        <text v-for="column in columns" :key="`a-${column}`" :x="cellCenter(column, 1).x" y="17">
          A{{ column }}
        </text>
        <text v-for="row in rows" :key="`b-${row}`" x="12" :y="cellCenter(1, row).y + 4">
          B{{ row }}
        </text>
      </g>

      <g class="cells">
        <g
          v-for="cell in cells"
          :key="cell.code"
          :data-testid="`cell-${cell.code}`"
          :data-cell="cell.code"
          :class="['cell', { 'no-fly': selectedNoFlySet.has(cell.code), editable }]"
          role="button"
          tabindex="0"
          :aria-label="`${cell.code}${selectedNoFlySet.has(cell.code) ? '，已选禁飞格' : ''}`"
          :aria-pressed="selectedNoFlySet.has(cell.code)"
          :aria-disabled="!editable"
          @click="activateCell(cell.code)"
          @keydown="onCellKeydown($event, cell.code)"
          @touchend.prevent="onCellTouch(cell.code)"
        >
          <rect :x="cell.x" :y="cell.y" width="50" height="50" />
          <text :x="cell.x + 25" :y="cell.y + 30" aria-hidden="true">{{ cell.code }}</text>
        </g>
      </g>
      <g class="selected-cell-overlays" aria-hidden="true">
        <rect
          v-for="cell in selectedCells"
          :key="`selected-${cell.code}`"
          data-testid="selected-cell-outline"
          :x="cell.x"
          :y="cell.y"
          width="50"
          height="50"
          class="selected-cell-outline"
        />
      </g>

      <polyline
        v-if="routePoints"
        data-testid="route"
        class="route"
        :points="routePoints"
        vector-effect="non-scaling-stroke"
      />
      <g v-for="segment in routeSegments" :key="segment.key" class="route-segments" aria-hidden="true">
        <line
          :data-testid="segment.repeated ? 'repeated-route-segment' : 'route-segment'"
          :class="['route-segment', { repeated: segment.repeated }]"
          :x1="segment.from.x + segment.offset.x"
          :y1="segment.from.y + segment.offset.y"
          :x2="segment.to.x + segment.offset.x"
          :y2="segment.to.y + segment.offset.y"
          vector-effect="non-scaling-stroke"
        />
        <path
          class="route-arrow"
          :class="{ repeated: segment.repeated }"
          :d="arrowPath(segment)"
        />
        <text
          v-if="segment.repeated && segment.passIndex === 1"
          class="route-badge"
          :x="segment.mid.x"
          :y="segment.mid.y - 5"
        >{{ segment.totalPasses }}x</text>
      </g>
      <polyline
        v-if="descentLine"
        class="descent-line"
        :points="descentLine"
        vector-effect="non-scaling-stroke"
      />

      <line
        v-if="descentMarkerOffset && descentStartPoint && descentDisplayPoint"
        data-testid="descent-marker-leader"
        class="marker-leader"
        :x1="descentStartPoint.x"
        :y1="descentStartPoint.y"
        :x2="descentDisplayPoint.x"
        :y2="descentDisplayPoint.y"
        vector-effect="non-scaling-stroke"
      />
      <line
        v-if="touchdownMarkerOffset && touchdownPoint && touchdownDisplayPoint"
        data-testid="touchdown-leader"
        class="marker-leader touchdown-leader"
        :x1="touchdownPoint.x"
        :y1="touchdownPoint.y"
        :x2="touchdownDisplayPoint.x"
        :y2="touchdownDisplayPoint.y"
        vector-effect="non-scaling-stroke"
      />

      <g
        v-if="currentPoint"
        data-marker="drone"
        class="marker drone-marker"
        aria-label="无人机当前位置"
        :data-cell="currentCell"
      >
        <circle :cx="currentPoint.x" :cy="currentPoint.y" r="11" />
        <path :d="`M ${currentPoint.x - 6} ${currentPoint.y} h 12 M ${currentPoint.x} ${currentPoint.y - 6} v 12`" />
      </g>
      <g
        v-if="startPoint"
        data-marker="start"
        class="marker start-marker"
        aria-label="起点"
        :data-display-x="startPoint.x"
        :data-display-y="startPoint.y"
      >
        <circle :cx="startPoint.x" :cy="startPoint.y" r="9" />
        <path :d="`M ${startPoint.x - 4} ${startPoint.y} h 8 M ${startPoint.x} ${startPoint.y - 4} v 8`" />
      </g>
      <g
        v-if="descentDisplayPoint"
        data-marker="descent-start"
        class="marker descent-marker"
        aria-label="下降起点"
        :data-display-x="descentDisplayPoint.x"
        :data-display-y="descentDisplayPoint.y"
      >
        <path
          :d="`M ${descentDisplayPoint.x} ${descentDisplayPoint.y - 10} L ${descentDisplayPoint.x + 10} ${descentDisplayPoint.y + 8} L ${descentDisplayPoint.x - 10} ${descentDisplayPoint.y + 8} Z`"
        />
      </g>
      <g
        v-if="touchdownDisplayPoint"
        data-marker="touchdown"
        class="marker touchdown-marker"
        aria-label="真实触地点"
        :data-display-x="touchdownDisplayPoint.x"
        :data-display-y="touchdownDisplayPoint.y"
      >
        <circle :cx="touchdownDisplayPoint.x" :cy="touchdownDisplayPoint.y" r="10" />
        <path
          :d="`M ${touchdownDisplayPoint.x - 6} ${touchdownDisplayPoint.y - 6} L ${touchdownDisplayPoint.x + 6} ${touchdownDisplayPoint.y + 6} M ${touchdownDisplayPoint.x + 6} ${touchdownDisplayPoint.y - 6} L ${touchdownDisplayPoint.x - 6} ${touchdownDisplayPoint.y + 6}`"
        />
      </g>
    </svg>

    <div class="map-legend" aria-label="地图图例">
      <span><i class="legend-symbol start" />起点</span>
      <span><i class="legend-symbol descent" />下降起点</span>
      <span><i class="legend-symbol touchdown" />真实触地点</span>
      <span><i class="legend-symbol no-fly" />禁飞格</span>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { QIcon } from 'quasar';

interface MissionWaypoint {
  id?: unknown;
  sequence_index?: unknown;
  action?: unknown;
}

interface MissionPlan {
  start_waypoint_id?: unknown;
  metadata_json?: unknown;
  waypoints?: unknown;
}

interface Point {
  x: number;
  y: number;
}

const props = defineProps<{
  plan: MissionPlan | null;
  selectedNoFlyCells: string[];
  editable: boolean;
  currentCell?: string | null;
}>();

const emit = defineEmits<{
  'toggle-cell': [cell: string];
}>();

const columns = Array.from({ length: 9 }, (_, index) => index + 1);
const rows = Array.from({ length: 7 }, (_, index) => index + 1);
const fullViewBox = { x: 0, y: 0, width: 500, height: 410 };
const markerClearance = 25;
const markerMargin = 12;
const markerOffsets: Point[] = [
  { x: -30, y: 0 },
  { x: 0, y: 30 },
  { x: 0, y: -30 },
  { x: 30, y: 0 },
  { x: -30, y: 30 },
  { x: -30, y: -30 },
  { x: 30, y: 30 },
  { x: 30, y: -30 },
  { x: -60, y: 0 },
  { x: 0, y: 60 },
  { x: 0, y: -60 },
  { x: 60, y: 0 },
];
const currentViewBox = ref({ ...fullViewBox });
const lastTouchAt = ref(0);

const cells = columns.flatMap((column) =>
  rows.map((row) => ({
    code: `A${column}B${row}`,
    x: 25 + (column - 1) * 50,
    y: 25 + (7 - row) * 50,
  })),
);

const selectedNoFlySet = computed(() => new Set(props.selectedNoFlyCells));
const selectedCells = computed(() => cells.filter((cell) => selectedNoFlySet.value.has(cell.code)));
const viewBox = computed(() => {
  const box = currentViewBox.value;
  return `${box.x} ${box.y} ${box.width} ${box.height}`;
});

const metadata = computed<Record<string, unknown>>(() => {
  if (typeof props.plan?.metadata_json !== 'string') return {};
  try {
    const parsed: unknown = JSON.parse(props.plan.metadata_json);
    return typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed)
      ? (parsed as Record<string, unknown>)
      : {};
  } catch {
    return {};
  }
});

const routeCells = computed(() => {
  if (!Array.isArray(props.plan?.waypoints)) return [];
  return (props.plan.waypoints as MissionWaypoint[])
    .slice()
    .sort((left, right) => numeric(left.sequence_index) - numeric(right.sequence_index))
    .map((waypoint) => (typeof waypoint.id === 'string' ? decodeCell(waypoint.id) : null))
    .filter((point): point is Point => point !== null);
});

const routePoints = computed(() =>
  routeCells.value.map((point) => `${point.x},${point.y}`).join(' '),
);
interface RouteSegment {
  key: string;
  from: Point;
  to: Point;
  mid: Point;
  offset: Point;
  repeated: boolean;
  passIndex: number;
  totalPasses: number;
}

const routeSegments = computed<RouteSegment[]>(() => {
  const points = routeCells.value;
  const counts = new Map<string, number>();
  for (let index = 1; index < points.length; index += 1) {
    const key = edgeKey(points[index - 1], points[index]);
    counts.set(key, (counts.get(key) ?? 0) + 1);
  }
  const passes = new Map<string, number>();
  return points.slice(1).map((to, index) => {
    const from = points[index];
    const key = edgeKey(from, to);
    const totalPasses = counts.get(key) ?? 1;
    const passIndex = (passes.get(key) ?? 0) + 1;
    passes.set(key, passIndex);
    const repeated = totalPasses > 1;
    const direction = { x: to.x - from.x, y: to.y - from.y };
    const length = Math.hypot(direction.x, direction.y) || 1;
    const lane = repeated ? (passIndex - (totalPasses + 1) / 2) * 7 : 0;
    const offset = { x: (-direction.y / length) * lane, y: (direction.x / length) * lane };
    return {
      key: `${key}-${passIndex}`,
      from,
      to,
      mid: { x: (from.x + to.x) / 2, y: (from.y + to.y) / 2 },
      offset,
      repeated,
      passIndex,
      totalPasses,
    };
  });
});
const startPoint = computed(() => {
  const id = metadata.value.start_cell ?? props.plan?.start_waypoint_id;
  return typeof id === 'string' ? decodeCell(id) : (routeCells.value[0] ?? null);
});
const currentPoint = computed(() =>
  typeof props.currentCell === 'string' ? decodeCell(props.currentCell) : null,
);
const descentStartPoint = computed(() => {
  const terminalCell = metadata.value.terminal_cell;
  return typeof terminalCell === 'string' ? decodeCell(terminalCell) : null;
});
const touchdownPoint = computed(() => {
  // The mission contract renders touchdown at the takeoff/start cell.
  // touchdown_x/y are physical centimetres used by protocol validation only.
  return startPoint.value;
});
const descentDisplayPoint = computed(() => {
  return descentStartPoint.value;
});
const touchdownDisplayPoint = computed(() => {
  if (!touchdownPoint.value) return null;
  return touchdownPoint.value;
});
const descentMarkerOffset = computed(
  () => false,
);
const touchdownMarkerOffset = computed(
  () =>
    false,
);
const descentLine = computed(() => {
  if (!descentStartPoint.value || !startPoint.value) return '';
  return `${descentStartPoint.value.x},${descentStartPoint.value.y} ${startPoint.value.x},${startPoint.value.y}`;
});

const controls = [
  { label: '放大地图', icon: 'add', action: () => zoom(0.8) },
  { label: '缩小地图', icon: 'remove', action: () => zoom(1.25) },
  { label: '适配地图', icon: 'fit_screen', action: fit },
];

function numeric(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : Number.MAX_SAFE_INTEGER;
}

function cellCenter(column: number, row: number): Point {
  return { x: 50 + (column - 1) * 50, y: 50 + (7 - row) * 50 };
}

function decodeCell(code: string): Point | null {
  const match = /^A([1-9])B([1-7])$/.exec(code);
  if (!match) return null;
  return cellCenter(Number(match[1]), Number(match[2]));
}

function edgeKey(left: Point, right: Point): string {
  const a = `${left.x},${left.y}`;
  const b = `${right.x},${right.y}`;
  return a < b ? `${a}|${b}` : `${b}|${a}`;
}

function arrowPath(segment: RouteSegment): string {
  const from = { x: segment.from.x + segment.offset.x, y: segment.from.y + segment.offset.y };
  const to = { x: segment.to.x + segment.offset.x, y: segment.to.y + segment.offset.y };
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const length = Math.hypot(dx, dy) || 1;
  const ux = dx / length;
  const uy = dy / length;
  const tip = { x: from.x + dx * 0.72, y: from.y + dy * 0.72 };
  const base = { x: tip.x - ux * 9, y: tip.y - uy * 9 };
  const nx = -uy;
  const ny = ux;
  return `M ${tip.x} ${tip.y} L ${base.x + nx * 4} ${base.y + ny * 4} L ${base.x - nx * 4} ${base.y - ny * 4} Z`;
}

function placeMarker(anchor: Point, occupied: Point[]): Point {
  const candidates = [{ x: 0, y: 0 }, ...markerOffsets].map((offset) =>
    boundMarker({ x: anchor.x + offset.x, y: anchor.y + offset.y }),
  );
  const clear = candidates.find((candidate) =>
    occupied.every((point) => pointDistance(candidate, point) >= markerClearance),
  );
  if (clear) return clear;

  return candidates.reduce((best, candidate) =>
    minimumDistance(candidate, occupied) > minimumDistance(best, occupied) ? candidate : best,
  );
}

function boundMarker(point: Point): Point {
  return {
    x: Math.min(fullViewBox.width - markerMargin, Math.max(markerMargin, point.x)),
    y: Math.min(fullViewBox.height - markerMargin, Math.max(markerMargin, point.y)),
  };
}

function minimumDistance(point: Point, occupied: Point[]): number {
  return occupied.length === 0
    ? Number.POSITIVE_INFINITY
    : Math.min(...occupied.map((other) => pointDistance(point, other)));
}

function pointDistance(left: Point, right: Point): number {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

function samePoint(left: Point, right: Point): boolean {
  return left.x === right.x && left.y === right.y;
}

function activateCell(code: string): void {
  if (!props.editable || Date.now() - lastTouchAt.value < 500) return;
  emit('toggle-cell', code);
}

function onCellKeydown(event: KeyboardEvent, code: string): void {
  if (event.key !== 'Enter' && event.key !== ' ') return;
  event.preventDefault();
  if (props.editable) emit('toggle-cell', code);
}

function onCellTouch(code: string): void {
  if (!props.editable) return;
  lastTouchAt.value = Date.now();
  emit('toggle-cell', code);
}

function zoom(scale: number): void {
  const box = currentViewBox.value;
  const width = Math.min(fullViewBox.width * 1.25, Math.max(180, box.width * scale));
  const height = width * (fullViewBox.height / fullViewBox.width);
  currentViewBox.value = {
    x: box.x + (box.width - width) / 2,
    y: box.y + (box.height - height) / 2,
    width,
    height,
  };
}

function fit(): void {
  currentViewBox.value = { ...fullViewBox };
}
</script>

<style scoped>
.mission-map {
  position: relative;
  display: flex;
  min-height: 0;
  flex-direction: column;
  min-width: 0;
  color: #dce4ea;
}

.map-canvas {
  display: block;
  width: 100%;
  min-height: 0;
  flex: 1 1 auto;
  max-height: none;
  aspect-ratio: 500 / 410;
  background: #101a1f;
  border: 1px solid #455a63;
  border-radius: 3px;
  touch-action: manipulation;
}

.field {
  fill: #172126;
  stroke: #71808a;
  stroke-width: 2;
}

.axis-labels text,
.cell text {
  fill: #aebbc3;
  font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
  font-size: 10px;
  letter-spacing: 0;
  text-anchor: middle;
  pointer-events: none;
}

.cell rect {
  fill: #18242a;
  stroke: #54636c;
  stroke-width: 1;
  transition: fill 120ms ease, stroke 120ms ease;
}

.cell.editable {
  cursor: pointer;
}

.cell.editable:hover rect,
.cell:focus-visible rect {
  fill: #24353d;
  stroke: #f4c95d;
  stroke-width: 3;
}

.cell.no-fly rect {
  fill: #733a32;
  stroke: #ff9b84;
}

.cell.no-fly text {
  fill: #fff4ef;
}

.selected-cell-outline {
  fill: none;
  stroke: #ff9b84;
  stroke-width: 2.5;
  pointer-events: none;
  vector-effect: non-scaling-stroke;
}

.route {
  fill: none;
  stroke: #2f6fed;
  stroke-linecap: round;
  stroke-linejoin: round;
  stroke-width: 2;
  opacity: 0.4;
  pointer-events: none;
}

.route-segment {
  stroke: #2f6fed;
  stroke-linecap: round;
  stroke-width: 3;
}

.route-segment.repeated {
  stroke: #f08c00;
  stroke-width: 5;
}

.route-arrow {
  fill: #2f6fed;
  stroke: none;
}

.route-arrow.repeated {
  fill: #f08c00;
  stroke: #fff;
  stroke-width: 1;
}

.route-badge {
  fill: #fff3d6;
  stroke: #f08c00;
  stroke-width: 1;
  font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
  font-size: 11px;
  font-weight: 700;
  text-anchor: middle;
  dominant-baseline: middle;
  paint-order: stroke;
  pointer-events: none;
}

.descent-line {
  fill: none;
  stroke: #f4c95d;
  stroke-dasharray: 7 5;
  stroke-width: 3;
  pointer-events: none;
}

.marker-leader {
  stroke: #f4c95d;
  stroke-width: 1.5;
  pointer-events: none;
}

.touchdown-leader {
  stroke: #ef6a59;
}

.marker {
  pointer-events: none;
}

.marker path,
.marker circle {
  vector-effect: non-scaling-stroke;
}

.start-marker circle {
  fill: #1d6a56;
  stroke: #b9ffe9;
  stroke-width: 2;
}

.start-marker path {
  stroke: #fff;
  stroke-width: 2;
}

.descent-marker path {
  fill: #f4c95d;
  stroke: #171717;
  stroke-width: 2;
}

.touchdown-marker circle {
  fill: #10171c;
  stroke: #ef6a59;
  stroke-width: 3;
}

.touchdown-marker path {
  stroke: #fff;
  stroke-width: 2;
}

.drone-marker circle {
  fill: #ef6a59;
  stroke: #fff;
  stroke-width: 2.5;
}

.drone-marker path {
  stroke: #fff;
  stroke-width: 2;
}

.map-toolbar {
  position: absolute;
  z-index: 2;
  top: 10px;
  right: 10px;
  display: flex;
  gap: 6px;
}

.map-control {
  display: grid;
  width: 44px;
  height: 44px;
  padding: 0;
  place-items: center;
  color: #f4f7f8;
  background: #26333a;
  border: 1px solid #697982;
  border-radius: 4px;
  cursor: pointer;
}

.map-control:hover,
.map-control:focus-visible {
  color: #10171c;
  background: #f4c95d;
  outline: none;
}

.map-legend {
  display: flex;
  min-height: 28px;
  flex-wrap: wrap;
  gap: 10px 18px;
  align-items: center;
  padding: 8px 2px 0;
  color: #abb8c0;
  font-size: 11px;
}

.map-legend span {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.legend-symbol {
  display: inline-block;
  width: 12px;
  height: 12px;
  box-sizing: border-box;
}

.legend-symbol.start {
  background: #45c7a0;
  border-radius: 50%;
}

.legend-symbol.descent {
  background: #f4c95d;
  clip-path: polygon(50% 0, 100% 100%, 0 100%);
}

.legend-symbol.touchdown {
  border: 2px solid #ef6a59;
  border-radius: 50%;
}

.legend-symbol.no-fly {
  background: #733a32;
  border: 1px solid #ff9b84;
}
</style>
