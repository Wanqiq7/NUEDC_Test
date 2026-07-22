#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$repo_root/scripts/tests/test_web_only_ground_station.sh"

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT

assert_forbidden() {
    local name=$1
    local claim=$2
    local probe="$tmp_dir/$name.md"
    printf '%s\n' "$claim" >"$probe"

    if ! scan_obsolete_current_docs "$probe" >/dev/null; then
        echo "expected forbidden planner claim to be detected: $claim" >&2
        return 1
    fi
}

assert_allowed() {
    local name=$1
    local claim=$2
    local probe="$tmp_dir/$name.md"
    printf '%s\n' "$claim" >"$probe"

    local scan_status
    if scan_obsolete_current_docs "$probe" >/dev/null; then
        echo "expected legitimate documentation to pass: $claim" >&2
        return 1
    else
        scan_status=$?
    fi
    if [[ $scan_status -ne 1 ]]; then
        echo "legitimate documentation scan failed with rg exit $scan_status: $claim" >&2
        return "$scan_status"
    fi
}

assert_forbidden planner_requires_qt_core "The planner requires Qt Core."
assert_forbidden planner_requires_qt "The planner requires Qt."
assert_forbidden planner_depends_on_qt6 "The planner depends on Qt6."
assert_forbidden planner_uses_qttest "The planner uses QtTest."
assert_forbidden planner_uses_cpp_protobuf "The planner uses C++ Protobuf."
assert_forbidden zeromq_required "ZeroMQ is required by the planner."
assert_forbidden simulator_required "The simulator is required by the planner."
assert_forbidden command_handler_owned "The command handler is owned by the planner."
assert_forbidden planner_owns_commands "The planner owns command handling."
assert_forbidden planner_uses_competition_core "The planner uses competition_core."
assert_forbidden cpp_planner_requires_protobuf "The C++ planner requires Protobuf."
assert_forbidden cpp_planner_uses_protobuf "The C++ planner uses Protobuf."
assert_forbidden protobuf_required_by_cpp_planner "Protobuf is required by the C++ planner."
assert_forbidden protobuf_used_by_cpp_planner "Protobuf is used by the C++ planner."

assert_allowed gateway_uses_zeromq "The Gateway uses ZeroMQ and Protobuf for airborne communication."
assert_allowed planner_without_qt "The planner does not require Qt."
assert_allowed historical_boundary "Historical docs describe the retired Qt planner architecture."

if scan_obsolete_current_docs "$tmp_dir/missing.md" >/dev/null 2>&1; then
    echo "expected a missing scan target to fail" >&2
    exit 1
else
    scan_status=$?
fi
if [[ $scan_status -le 1 ]]; then
    echo "expected scan error status to propagate, got $scan_status" >&2
    exit 1
fi

echo "current-document boundary scan self-test passed"
