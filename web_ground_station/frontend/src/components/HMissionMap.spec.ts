// @vitest-environment jsdom

import { mount } from '@vue/test-utils';
import { Quasar } from 'quasar';
import { describe, expect, it } from 'vitest';

import HMissionMap from './HMissionMap.vue';

const plan = {
  message_type: 'task_plan',
  task_id: 'wildlife-demo',
  task_type: 'h_problem',
  start_waypoint_id: 'A9B1',
  terminal_waypoint_id: 'touchdown',
  metadata_json: JSON.stringify({
    start_cell: 'A9B1',
    terminal_cell: 'A8B4',
    touchdown_x_cm: 444.3,
    touchdown_y_cm: 332.9,
  }),
  waypoints: [
    { id: 'A9B1', sequence_index: 0, x: 0, y: 0, z: 1.2, action: 'takeoff' },
    { id: 'A8B1', sequence_index: 1, x: 0, y: 0.5, z: 1.2, action: 'navigate' },
    { id: 'A8B4', sequence_index: 2, x: 1.5, y: 0.5, z: 1.2, action: 'navigate' },
    { id: 'touchdown', sequence_index: 3, x: 0.17, y: 0.06, z: 0, action: 'land' },
  ],
};

const mountMap = (props: InstanceType<typeof HMissionMap>['$props']) =>
  mount(HMissionMap, { props, global: { plugins: [Quasar] } });

describe('HMissionMap', () => {
  it('renders 63 stable grid cells and distinct landing markers', () => {
    const wrapper = mountMap({ plan, selectedNoFlyCells: ['A2B2'], editable: true });

    expect(wrapper.findAll('[data-cell]')).toHaveLength(63);
    expect(wrapper.get('[data-cell="A2B2"]').classes()).toContain('no-fly');
    expect(wrapper.get('[data-marker="start"]').exists()).toBe(true);
    expect(wrapper.get('[data-marker="descent-start"]').exists()).toBe(true);
    expect(wrapper.get('[data-marker="touchdown"]').exists()).toBe(true);
    expect(wrapper.get('[data-testid="route"]').attributes('points')).toBe(
      '450,350 400,350 400,200',
    );
  });

  it('emits one cell toggle from click, keyboard, and touch activation', async () => {
    const wrapper = mountMap({ plan: null, selectedNoFlyCells: [], editable: true });
    const cell = wrapper.get('[data-cell="A2B2"]');

    await cell.trigger('click');
    await cell.trigger('keydown', { key: 'Enter' });
    await cell.trigger('keydown', { key: ' ' });
    await cell.trigger('touchend');

    expect(wrapper.emitted('toggle-cell')).toEqual([
      ['A2B2'],
      ['A2B2'],
      ['A2B2'],
      ['A2B2'],
    ]);
  });

  it('does not activate cells when editing is disabled', async () => {
    const wrapper = mountMap({ plan: null, selectedNoFlyCells: [], editable: false });

    await wrapper.get('[data-cell="A2B2"]').trigger('click');

    expect(wrapper.emitted('toggle-cell')).toBeUndefined();
  });

  it('offers explicit zoom and fit controls without changing the plan', async () => {
    const wrapper = mountMap({ plan, selectedNoFlyCells: [], editable: true });
    const originalRoute = wrapper.get('[data-testid="route"]').attributes('points');
    const originalViewBox = wrapper.get('svg').attributes('viewBox');

    await wrapper.get('[aria-label="放大地图"]').trigger('click');
    expect(wrapper.get('svg').attributes('viewBox')).not.toBe(originalViewBox);
    await wrapper.get('[aria-label="缩小地图"]').trigger('click');
    await wrapper.get('[aria-label="适配地图"]').trigger('click');

    expect(wrapper.get('svg').attributes('viewBox')).toBe(originalViewBox);
    expect(wrapper.get('[data-testid="route"]').attributes('points')).toBe(originalRoute);
  });

  it.each([
    { touchdown_x_cm: 450, touchdown_y_cm: 350 },
    { touchdown_x_cm: 444, touchdown_y_cm: 344 },
  ])(
    'keeps touchdown marker in the Qt start cell regardless of physical coordinates (%o)',
    (touchdown) => {
      const collisionPlan = {
        ...plan,
        metadata_json: JSON.stringify({
          terminal_cell: 'A9B1',
          ...touchdown,
        }),
      };
      const wrapper = mountMap({
        plan: collisionPlan,
        selectedNoFlyCells: [],
        editable: false,
      });
      const start = wrapper.get('[data-marker="start"]');
      const descent = wrapper.get('[data-marker="descent-start"]');
      const touchdownMarker = wrapper.get('[data-marker="touchdown"]');
      const positions = [start, descent, touchdownMarker].map(
        (marker) => `${marker.attributes('data-display-x')},${marker.attributes('data-display-y')}`,
      );
      expect(positions[0]).toBe(positions[2]);
      expect(wrapper.find('[data-testid="touchdown-leader"]').exists()).toBe(false);
      expect(wrapper.get('[data-testid="route"]').attributes('points')).toBe(
        '450,350 400,350 400,200',
      );
    },
  );

  it('marks repeated undirected edges with separate lanes, arrows, and a pass badge', () => {
    const repeatedPlan = {
      ...plan,
      waypoints: [
        { id: 'A9B1', sequence_index: 0 },
        { id: 'A8B1', sequence_index: 1 },
        { id: 'A9B1', sequence_index: 2 },
        { id: 'A8B1', sequence_index: 3 },
        { id: 'A9B1', sequence_index: 4 },
      ],
    };
    const wrapper = mountMap({ plan: repeatedPlan, selectedNoFlyCells: [], editable: false });
    expect(wrapper.findAll('[data-testid="repeated-route-segment"]')).toHaveLength(4);
    expect(wrapper.findAll('.route-badge')).toHaveLength(1);
    const lines = wrapper.findAll('[data-testid="repeated-route-segment"]');
    expect(lines[0].attributes('y1')).not.toBe(lines[1].attributes('y1'));
  });
});
