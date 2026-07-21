#!/usr/bin/env bash
set -euo pipefail

scan_forbidden_references() {
    # Return rg's status unchanged: 0 = matches, 1 = no matches, >1 = scan error.
    rg -n \
        --glob '!scripts/tests/test_web_only_ground_station.sh' \
        --glob '!docs/**' \
        --glob '!**/.generated/**' \
        --glob '!**/build/**' \
        --glob '!**/.venv/**' \
        --glob '!**/vendor/**' \
        'Qt6|QtTest|CMAKE_AUTOMOC|Q_OBJECT|#[[:space:]]*include[[:space:]]*(<Q[^>]*>|"Q[^"]*")' \
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
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
