# Route Length Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the continuous coverage route length for the H-problem sample obstacle layout without breaking adjacency, no-fly avoidance, or full coverage.

**Architecture:** Keep the existing continuous DFS-style coverage planner, but replace fixed neighbor ordering with a branch-size heuristic that visits smaller branches first and leaves the largest reachable branch for the end. This preserves correctness while cutting unnecessary backtracking. UI and simulator interfaces remain unchanged because the route format is still an ordered cell list.

**Tech Stack:** Python 3.10, unittest, Qt6, CMake

---

### Task 1: Lock optimization targets with failing tests

**Files:**
- Modify: `python/tests/test_route_planner.py`

- [ ] **Step 1: Add a route-length regression test for the sample three-cell obstacle**
- [ ] **Step 2: Run `python3 -m unittest discover -s python/tests -v` and confirm the old planner fails the length target**

### Task 2: Implement the optimized neighbor ordering

**Files:**
- Modify: `python/uav_testbed/route_planner.py`

- [ ] **Step 1: Add a helper that estimates the reachable branch size behind each candidate neighbor**
- [ ] **Step 2: Update neighbor ordering to visit smaller branches first and keep the largest branch for the end**
- [ ] **Step 3: Re-run `python3 -m unittest discover -s python/tests -v` and confirm adjacency, coverage, and route-length tests pass**

### Task 3: Re-verify integration invariants

**Files:**
- No code changes expected unless regression found

- [ ] **Step 1: Run `ctest --test-dir build --output-on-failure`**
- [ ] **Step 2: Print the optimized sample route and confirm total route length is lower than the previous `81`**
- [ ] **Step 3: Re-run the simulator and ground station once to confirm database writes still succeed**
