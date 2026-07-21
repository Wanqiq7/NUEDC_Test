#!/usr/bin/env python3

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path
from typing import Any


def compare_json(expected: Any, actual: Any, path: str = "$") -> list[str]:
    if isinstance(expected, bool) or isinstance(actual, bool):
        if type(expected) is not type(actual) or expected != actual:
            return [f"{path}: expected {expected!r}, got {actual!r}"]
        return []

    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        if not math.isclose(expected, actual, rel_tol=1e-12, abs_tol=1e-9):
            return [f"{path}: expected {expected!r}, got {actual!r}"]
        return []

    if isinstance(expected, dict) and isinstance(actual, dict):
        errors = []
        expected_keys = set(expected)
        actual_keys = set(actual)
        for key in sorted(expected_keys - actual_keys):
            errors.append(f"{path}: missing key {key!r}")
        for key in sorted(actual_keys - expected_keys):
            errors.append(f"{path}: unexpected key {key!r}")
        for key in sorted(expected_keys & actual_keys):
            errors.extend(compare_json(expected[key], actual[key], f"{path}.{key}"))
        return errors

    if isinstance(expected, list) and isinstance(actual, list):
        errors = []
        if len(expected) != len(actual):
            errors.append(f"{path}: expected {len(expected)} items, got {len(actual)}")
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            errors.extend(compare_json(expected_item, actual_item, f"{path}[{index}]"))
        return errors

    if type(expected) is not type(actual) or expected != actual:
        return [f"{path}: expected {expected!r}, got {actual!r}"]
    return []


def verify_case(
    planner: Path,
    case: dict[str, Any],
    repository_root: Path,
) -> list[str]:
    name = case.get("name", "<unnamed>")
    errors = []
    result = subprocess.run(
        [str(planner)],
        input=case["stdin_text"],
        text=True,
        capture_output=True,
        cwd=repository_root,
        check=False,
    )

    if result.returncode != case["exit_code"]:
        errors.append(
            f"exit code: expected {case['exit_code']}, got {result.returncode}"
        )

    try:
        response = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        errors.append(f"stdout is not exactly one JSON document: {error}")
    else:
        errors.extend(compare_json(case["response"], response))

    if case.get("stderr_json_forbidden", False) and "{" in result.stderr:
        errors.append("stderr contains forbidden '{'")

    return [f"{name}: {error}" for error in errors]


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify planner CLI golden behavior")
    parser.add_argument("planner", type=Path)
    parser.add_argument("fixture", type=Path)
    args = parser.parse_args()

    repository_root = Path(__file__).resolve().parents[3]
    planner = (
        args.planner if args.planner.is_absolute() else repository_root / args.planner
    )
    fixture = args.fixture if args.fixture.is_absolute() else repository_root / args.fixture

    try:
        cases = json.loads(fixture.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"failed to load fixture {fixture}: {error}", file=sys.stderr)
        return 1

    if not isinstance(cases, list):
        print(f"fixture {fixture} must contain a JSON array", file=sys.stderr)
        return 1

    failures = []
    for case in cases:
        if not isinstance(case, dict):
            failures.append("fixture case must be a JSON object")
            continue
        try:
            failures.extend(verify_case(planner, case, repository_root))
        except (KeyError, OSError) as error:
            failures.append(f"{case.get('name', '<unnamed>')}: {error}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1

    print(f"verified {len(cases)} planner golden cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
