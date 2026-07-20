# Whole-Branch Review Fix Report

## Outcome

- Gateway Web sequence is now an independent, monotonic JavaScript-safe domain. Airborne command/event sequences are retained only for source deduplication and ACK idempotency.
- Planning and LOAD are rejected by both server and UI unless the command link is online and the airborne mission is stopped. Planning rechecks state before applying its result.
- Restart task mismatches are explicit (`task_sync_state`, `airborne_task_id`). A mismatched disk plan never inherits airborne loaded/running state, START remains disabled, and STOP targets the actual airborne task.
- Qt and Web document and test the shared `(Unix epoch ms << 20) + process counter` command sequence contract; simultaneous senders remain prohibited.
- The planning panel accepts a selectable case path under `shared/cases/`, and the status bar provides an explicit command-link probe control.
- PID visualization remains outside the Web implementation; PlotJuggler continues to consume UDP `9870`.

## Verification

- `UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run pytest tests/gateway -q`: 107 passed.
- `UV_CACHE_DIR=/tmp/nuedc-uv-cache uv run ruff check gateway tests/gateway`: passed.
- `corepack pnpm test -- --run`: 44 passed.
- `corepack pnpm typecheck`: passed.
- `corepack pnpm build`: passed.
- `bash -n web_ground_station/scripts/start_dev.sh web_ground_station/scripts/start_competition.sh web_ground_station/scripts/check_web_ground_station.sh`: passed.
- `bash web_ground_station/scripts/check_web_ground_station.sh`: offline preflight passed.
- `git diff --check`: passed.

## Residual Environment Constraint

Responsive Playwright/browser screenshots were not rerun in this fix wave. The existing environment rejected the Vite listen operation with `EPERM`, and the prior escalation attempt failed externally. No indirect server workaround was used. Loopback ZeroMQ tests were permitted in the final run and passed.
