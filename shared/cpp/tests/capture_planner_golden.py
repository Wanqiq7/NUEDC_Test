#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


SCHEMA = "h_planning_request_v1"
SAMPLE_CASE = "shared/cases/sample_case.json"


def compact_request(**overrides: Any) -> str:
    request = {
        "schema": SCHEMA,
        "case_path": SAMPLE_CASE,
        "no_fly_cells": [],
    }
    request.update(overrides)
    return json.dumps(request, separators=(",", ":"), ensure_ascii=True)


def capture_inputs() -> list[tuple[str, str]]:
    return [
        (
            "sample_default_barrier",
            compact_request(no_fly_cells=["A4B3", "A5B3", "A6B3"]),
        ),
        (
            "barrier_a2b2_a2b3_a2b4",
            compact_request(no_fly_cells=["A2B2", "A2B3", "A2B4"]),
        ),
        ("raw_invalid_json", "{"),
        ("wrong_schema", compact_request(schema="wrong")),
        ("empty_case_path", compact_request(case_path="")),
        ("non_array_no_fly", compact_request(no_fly_cells="A2B2")),
        ("out_of_bounds_no_fly", compact_request(no_fly_cells=["A10B1"])),
        (
            "missing_case",
            compact_request(case_path="shared/cases/missing.json"),
        ),
        ("blocked_start", compact_request(no_fly_cells=["A9B1"])),
    ]


def capture_case(
    planner: Path,
    name: str,
    stdin_text: str,
    repository_root: Path,
) -> dict[str, Any]:
    result = subprocess.run(
        [str(planner)],
        input=stdin_text,
        text=True,
        capture_output=True,
        cwd=repository_root,
        check=False,
    )
    try:
        response = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        message = f"{name}: planner stdout is not exactly one JSON document: {error}"
        raise RuntimeError(message) from error

    return {
        "name": name,
        "stdin_text": stdin_text,
        "exit_code": result.returncode,
        "response": response,
        "stderr_json_forbidden": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture planner CLI golden behavior")
    parser.add_argument("--planner", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    repository_root = Path(__file__).resolve().parents[3]
    planner = (
        args.planner if args.planner.is_absolute() else repository_root / args.planner
    )
    output = args.output if args.output.is_absolute() else repository_root / args.output

    if output.exists() and not args.overwrite:
        print(f"refusing to overwrite existing fixture: {output}", file=sys.stderr)
        return 1
    if not planner.is_file():
        print(f"planner executable not found: {planner}", file=sys.stderr)
        return 1

    try:
        cases = [
            capture_case(planner, name, stdin_text, repository_root)
            for name, stdin_text in capture_inputs()
        ]
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(cases, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    print(f"captured {len(cases)} planner golden cases in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
