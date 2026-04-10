# H Wildlife Mixed Testbed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a runnable mixed-process testbed for the 2025 H wildlife patrol problem using a Python aircraft simulator and a Qt6 desktop ground station over ZeroMQ + Protobuf.

**Architecture:** The project is split into a Python simulator, a Qt6 ground station, generated protobuf bindings, and reusable JSON test cases. The first version validates the one-way reporting path from simulated aircraft to ground station, including route planning, live display, SQLite persistence, and mission summary.

**Tech Stack:** Python 3.10, unittest, ZeroMQ, Protobuf v3, C++17, Qt6 Widgets, Qt Test, SQLite, CMake

---

### Task 1: Create project skeleton and sample case

**Files:**
- Create: `CMakeLists.txt`
- Create: `proto/messages.proto`
- Create: `cases/sample_case.json`
- Create: `README.md`

- [ ] **Step 1: Create the directory layout and root build entry**
- [ ] **Step 2: Add the protobuf schema for config, telemetry, detection, and summary**
- [ ] **Step 3: Add one deterministic sample case matching the contest grid**
- [ ] **Step 4: Document how to build and run both processes**

### Task 2: Add failing Python tests for planning and simulation

**Files:**
- Create: `python/tests/test_route_planner.py`
- Create: `python/tests/test_simulator.py`

- [ ] **Step 1: Write a failing route coverage test**
- [ ] **Step 2: Run it and confirm the planner does not exist yet**
- [ ] **Step 3: Write a failing simulator detection test**
- [ ] **Step 4: Run it and confirm the simulator module does not exist yet**

### Task 3: Implement Python simulator

**Files:**
- Create: `python/uav_testbed/case_loader.py`
- Create: `python/uav_testbed/models.py`
- Create: `python/uav_testbed/route_planner.py`
- Create: `python/uav_testbed/simulator.py`
- Create: `python/uav_testbed/publisher.py`
- Create: `python/uav_testbed/run_simulator.py`

- [ ] **Step 1: Implement the minimal planner to satisfy the failing route test**
- [ ] **Step 2: Re-run the route test and verify it passes**
- [ ] **Step 3: Implement the minimal simulator to satisfy the failing detection test**
- [ ] **Step 4: Re-run the simulator test and verify it passes**
- [ ] **Step 5: Add publisher integration and CLI entrypoint**

### Task 4: Add failing C++ tests for storage and grid mapping

**Files:**
- Create: `ground_station/tests/test_detection_repository.cpp`
- Create: `ground_station/tests/test_grid_mapper.cpp`

- [ ] **Step 1: Write a failing repository aggregation test**
- [ ] **Step 2: Build and confirm the repository implementation does not exist yet**
- [ ] **Step 3: Write a failing grid code mapping test**
- [ ] **Step 4: Build and confirm the mapper implementation does not exist yet**

### Task 5: Implement Qt6 ground station

**Files:**
- Create: `ground_station/src/main.cpp`
- Create: `ground_station/src/main_window.h`
- Create: `ground_station/src/main_window.cpp`
- Create: `ground_station/src/grid_scene.h`
- Create: `ground_station/src/grid_scene.cpp`
- Create: `ground_station/src/grid_mapper.h`
- Create: `ground_station/src/grid_mapper.cpp`
- Create: `ground_station/src/detection_repository.h`
- Create: `ground_station/src/detection_repository.cpp`
- Create: `ground_station/src/message_dispatcher.h`
- Create: `ground_station/src/message_dispatcher.cpp`
- Create: `ground_station/src/zmq_subscriber_worker.h`
- Create: `ground_station/src/zmq_subscriber_worker.cpp`

- [ ] **Step 1: Implement mapper and repository to satisfy the failing C++ tests**
- [ ] **Step 2: Rebuild and verify the unit tests pass**
- [ ] **Step 3: Implement the scene and main window with live widgets**
- [ ] **Step 4: Implement the ZeroMQ subscriber worker and protobuf dispatch**
- [ ] **Step 5: Wire persistence, live updates, and mission summary display**

### Task 6: Generate protobuf bindings and integrate both runtimes

**Files:**
- Modify: `CMakeLists.txt`
- Create: `proto/generated/.gitkeep`

- [ ] **Step 1: Add CMake custom commands to generate C++ bindings from `proto/messages.proto`**
- [ ] **Step 2: Add a build helper to generate Python protobuf bindings**
- [ ] **Step 3: Ensure both the simulator and ground station consume the same schema**

### Task 7: End-to-end verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Run Python unit tests**
- [ ] **Step 2: Build the Qt ground station and run C++ tests**
- [ ] **Step 3: Run the simulator against the sample case**
- [ ] **Step 4: Start the ground station and confirm it receives and persists detections**
- [ ] **Step 5: Record exact commands and expected outputs in the README**
