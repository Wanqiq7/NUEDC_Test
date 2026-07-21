#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

if [[ -d "$repo_root/ground_station_computer" ]]; then
    echo "legacy ground_station_computer source tree is still present" >&2
    exit 1
fi

if [[ -d "$repo_root/shared/cpp/include/competition_core" ]]; then
    echo "legacy competition_core headers are still present" >&2
    exit 1
fi

if rg -n --glob '!scripts/tests/test_web_only_ground_station.sh' \
    'Qt6|QtTest|CMAKE_AUTOMOC|Q_OBJECT|#include[[:space:]]*<Q' \
    "$repo_root/CMakeLists.txt" "$repo_root/shared/cpp" "$repo_root/web_ground_station" "$repo_root/scripts";
then
    echo "Qt-era native references remain in web-only ground station sources" >&2
    exit 1
fi

echo "web-only ground station source boundary is clean"
