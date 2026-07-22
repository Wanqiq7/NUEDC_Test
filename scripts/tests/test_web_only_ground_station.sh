#!/usr/bin/env bash
set -euo pipefail

scan_forbidden_references() {
    # 原样返回 rg 状态：0 表示有匹配，1 表示无匹配，大于 1 表示扫描错误。
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
    # 此策略检查排除历史设计记录；以下当前入口文档必须描述实际交付边界。
    local planner='(?:the[[:space:]]+)?(?:C\+\+[[:space:]]+)?planner(?:[[:space:]]+(?:library|CLI|core|component))?'
    local forbidden='(?:Qt(?:[[:space:]]+Core|6|Test)?|(?:C\+\+[[:space:]]+)?Protobuf|ZeroMQ|simulator|command[[:space:]]+(?:handler|handling)|competition_core)'
    local forward_relation='(?:requires?|depends[[:space:]]+on|owns?|uses?)'
    local passive_relation='(?:is|are)[[:space:]]+(?:required|depended[[:space:]]+on|owned|used)[[:space:]]+by'

    rg -n -i -P \
        "(?:${planner}[[:space:]]+${forward_relation}[[:space:]]+(?:the[[:space:]]+)?${forbidden}|${forbidden}[[:space:]]+${passive_relation}[[:space:]]+${planner})" \
        "$@"
}

main() {
    local repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}

    if [[ -d "$repo_root/ground_station_computer" ]]; then
        echo "旧版 ground_station_computer 源码树仍然存在" >&2
        return 1
    fi
    if [[ -d "$repo_root/shared/cpp/include/competition_core" ]]; then
        echo "旧版 competition_core 头文件仍然存在" >&2
        return 1
    fi

    local scan_status
    if scan_forbidden_references \
        "$repo_root/CMakeLists.txt" \
        "$repo_root/shared/cpp" \
        "$repo_root/web_ground_station" \
        "$repo_root/scripts"; then
        echo "Web-only 地面站源码中仍有 Qt 时代的原生代码引用" >&2
        return 1
    else
        scan_status=$?
    fi

    case "$scan_status" in
        1)
            echo "Web-only 地面站源码边界检查通过"
            ;;
        *)
            echo "扫描 Web-only 地面站源码失败（rg 退出码 $scan_status）" >&2
            return "$scan_status"
            ;;
    esac


    if scan_obsolete_current_docs \
        "$repo_root/README.md" \
        "$repo_root/shared/cpp/README.md" \
        "$repo_root/AGENTS.md"; then
        echo "当前文档中仍有过时的 Qt 时代规划器描述" >&2
        return 1
    else
        scan_status=$?
    fi

    case "$scan_status" in
        1)
            echo "当前文档已正确描述 Web-only 边界"
            ;;
        *)
            echo "扫描当前文档失败（rg 退出码 $scan_status）" >&2
            return "$scan_status"
            ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
