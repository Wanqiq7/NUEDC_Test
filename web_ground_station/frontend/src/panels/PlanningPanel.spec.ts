// @vitest-environment jsdom

import { flushPromises, mount } from '@vue/test-utils';
import { createPinia, setActivePinia } from 'pinia';
import { Quasar } from 'quasar';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import * as gatewayApi from '../api/gateway';
import { ApiError } from '../api/gateway';
import { useGroundStore } from '../stores/ground';
import PlanningPanel from './PlanningPanel.vue';

const successfulPlan = {
  message_type: 'task_plan',
  task_id: 'wildlife-demo',
  task_type: 'h_problem',
  start_waypoint_id: 'A9B1',
  terminal_waypoint_id: 'touchdown',
  metadata_json: JSON.stringify({
    no_fly_cells: ['A2B2', 'A2B3', 'A2B4'],
    terminal_cell: 'A8B4',
    touchdown_x_cm: 444.3,
    touchdown_y_cm: 332.9,
  }),
  waypoints: [
    { id: 'A9B1', sequence_index: 0, x: 0, y: 0, z: 1.2, action: 'takeoff' },
    { id: 'A8B4', sequence_index: 1, x: 1.5, y: 0.5, z: 1.2, action: 'navigate' },
    { id: 'touchdown', sequence_index: 2, x: 0.17, y: 0.06, z: 0, action: 'land' },
  ],
};

const mountPanel = () => mount(PlanningPanel, { global: { plugins: [Quasar] } });

describe('PlanningPanel', () => {
  beforeEach(() => {
    setActivePinia(createPinia());
    vi.restoreAllMocks();
  });

  it('enables planning only for three contiguous cells', async () => {
    const wrapper = mountPanel();
    await wrapper.get('[data-testid="edit-no-fly"]').trigger('click');

    await wrapper.get('[data-cell="A2B2"]').trigger('click');
    await wrapper.get('[data-cell="A2B3"]').trigger('click');
    await wrapper.get('[data-cell="A3B3"]').trigger('click');

    expect(wrapper.get('[data-testid="selection-status"]').text()).toContain(
      '必须横向或纵向连续',
    );
    expect(wrapper.get('[data-testid="generate-plan"]').attributes('disabled')).toBeDefined();

    await wrapper.get('[data-cell="A3B3"]').trigger('click');
    await wrapper.get('[data-cell="A2B4"]').trigger('click');

    expect(wrapper.get('[data-testid="selection-status"]').text()).toContain('可生成航线');
    expect(wrapper.get('[data-testid="generate-plan"]').attributes('disabled')).toBeUndefined();
  });

  it('submits selected cells to the authoritative planner and displays its plan', async () => {
    const planMission = vi
      .spyOn(gatewayApi, 'planMission')
      .mockResolvedValue({ ok: true, plan: successfulPlan });
    const wrapper = mountPanel();
    await wrapper.get('[data-testid="edit-no-fly"]').trigger('click');
    for (const cell of ['A2B2', 'A2B3', 'A2B4']) {
      await wrapper.get(`[data-cell="${cell}"]`).trigger('click');
    }

    await wrapper.get('[data-testid="generate-plan"]').trigger('click');
    await flushPromises();

    expect(planMission).toHaveBeenCalledWith({
      case_path: 'shared/cases/sample_case.json',
      no_fly_cells: ['A2B2', 'A2B3', 'A2B4'],
    });
    expect(useGroundStore().plan).toEqual(successfulPlan);
    expect(wrapper.get('[data-testid="planning-status"]').text()).toContain('航线已生成');
  });

  it('keeps candidate cells visible when the gateway rejects planning', async () => {
    vi.spyOn(gatewayApi, 'planMission').mockRejectedValue(
      new ApiError(409, 'planning_failed', '没有满足降落约束的路线'),
    );
    const wrapper = mountPanel();
    await wrapper.get('[data-testid="edit-no-fly"]').trigger('click');
    for (const cell of ['A2B2', 'A2B3', 'A2B4']) {
      await wrapper.get(`[data-cell="${cell}"]`).trigger('click');
    }

    await wrapper.get('[data-testid="generate-plan"]').trigger('click');
    await flushPromises();

    expect(wrapper.get('[data-testid="planning-error"]').text()).toContain(
      '没有满足降落约束的路线',
    );
    expect(wrapper.get('[data-cell="A2B2"]').classes()).toContain('no-fly');
    expect(wrapper.get('[data-testid="generate-plan"]').attributes('disabled')).toBeUndefined();
  });
});
