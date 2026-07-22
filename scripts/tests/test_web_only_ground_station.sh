#!/usr/bin/env bash
set -euo pipefail

scan_forbidden_references() {
    # Return rg's status unchanged: 0 = matches, 1 = no matches, >1 = scan error.
    rg -n \
        --glob '!scripts/tests/test_web_only_ground_station.sh' \
        --glob '!scripts/tests/test_web_only_ground_station_docs.sh' \
        --glob '!docs/**' \
        --glob '!**/.generated/**' \
        --glob '!**/build/**' \
        --glob '!**/.venv/**' \
        --glob '!**/vendor/**' \
        'Qt6|QtTest|CMAKE_AUTOMOC|Q_OBJECT|#[[:space:]]*include[[:space:]]*(<Q[^>]*>|"Q[^"]*")' \
        "$@"
}

scan_obsolete_current_docs() {
    # Keep historical design records out of this policy check. These are the
    # current entry-point documents that must describe the shipped boundary.
    local planner='(?:the[[:space:]]+)?(?:C\+\+[[:space:]]+)?planner(?:[[:space:]]+(?:library|CLI|core|component))?'
    local forbidden='(?:Qt(?:[[:space:]]+Core|6|Test)?|C\+\+[[:space:]]+Protobuf|ZeroMQ|simulator|command[[:space:]]+(?:handler|handling)|competition_core)'
    local forward_relation='(?:requires?|depends[[:space:]]+on|owns?|uses?)'
    local passive_relation='(?:is|are)[[:space:]]+(?:required|depended[[:space:]]+on|owned|used)[[:space:]]+by'

    rg -n -i -P \
        "(?:${planner}[[:space:]]+${forward_relation}[[:space:]]+(?:the[[:space:]]+)?${forbidden}|${forbidden}[[:space:]]+${passive_relation}[[:space:]]+${planner})" \
        "$@"
}

main() {
    local repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}

    if [[ -d "$repo_root/ground_station_computer" ]]; then
        echo "legacy ground_station_computer source tree is still present" >&2
        return 1
    fi
    if [[ -d "$repo_root/shared/cpp/include/competition_core" ]]; then
        echo "legacy competition_core headers are still present" >&2
        return 1
    fi

    local scan_status
    if scan_forbidden_references \
        "$repo_root/CMakeLists.txt" \
        "$repo_root/shared/cpp" \
        "$repo_root/web_ground_station" \
        "$repo_root/scripts"; then
        echo "Qt-era native references remain in web-only ground station sources" >&2
        return 1
    else
        scan_status=$?
    fi

    case "$scan_status" in
        1)
            echo "web-only ground station source boundary is clean"
            ;;
        *)
            echo "failed to scan web-only ground station sources (rg exit $scan_status)" >&2
            return "$scan_status"
            ;;
    esac


    if scan_obsolete_current_docs \
        "$repo_root/README.md" \
        "$repo_root/shared/cpp/README.md" \
        "$repo_root/AGENTS.md"; then
        echo "obsolete Qt-era planner claims remain in current documentation" >&2
        return 1
    else
        scan_status=$?
    fi

    case "$scan_status" in
        1)
            echo "current documentation describes the web-only boundary"
            ;;
        *)
            echo "failed to scan current documentation (rg exit $scan_status)" >&2
            return "$scan_status"
            ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
