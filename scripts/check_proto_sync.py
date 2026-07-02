#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 messages.proto 的三份副本是否一致。

背景：`messages.proto` 在仓库里有三份（有意为之，让机载端 colcon 与地面站 CMake
各自独立构建）：

  1. Ground/shared/proto/messages.proto                              —— 地面站
  2. Airborne/shared/proto/messages.proto                            —— 机载端共享副本
  3. Airborne/src/nuedc_airborne/nuedc_bridge/proto/messages.proto   —— 桥接节点副本

机载端已有单元测试（test_bridge_conversions.cpp）校验 #2 与 #3 一致，但 Ground 与
Airborne 是两个独立 git 仓库，**没有任何自动化手段**保证 #1 与 #2/#3 同步。一旦只改了
一端的 proto，protobuf 序列化会静默不兼容——不报编译错，只在运行时丢字段或错位解析，
是比赛现场最难排查的一类 bug。

本脚本作为跨仓库护栏，比较三份文件（忽略 CRLF/LF 行尾差异，因为 Windows 开发机与
Linux 机载端检出的行尾可能不同，而行尾不影响 protobuf 语义），任一不一致即以非零码退出。

**运行位置**：本脚本随地面站仓库（Ground/）发布，但校验需要同时看到 Ground 与 Airborne
两棵树。它会从脚本所在位置向上查找「同时包含 Ground/ 与 Airborne/ 的组合父目录」
（即 CLAUDE.md 描述的 `F:\\26_NUEDC\\Ground` 布局）。若只检出了地面站单仓库、找不到
Airborne 副本，则视为「无法跨仓库校验」并优雅跳过（退出码 0），避免单仓库 CI 误报失败。

用法：
    python scripts/check_proto_sync.py            # 校验；不一致退出 1，无法校验退出 0
    python scripts/check_proto_sync.py --quiet    # 仅在不一致时输出
    python scripts/check_proto_sync.py --strict    # 找不到 Airborne 副本时也判失败（退出 2）
"""
from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

# 三份副本相对「组合父目录」的路径。第一份作为基准，其余与之比对。
GROUND_PROTO = Path("Ground/shared/proto/messages.proto")
AIRBORNE_PROTOS = [
    Path("Airborne/shared/proto/messages.proto"),
    Path("Airborne/src/nuedc_airborne/nuedc_bridge/proto/messages.proto"),
]


def find_combined_root() -> Path | None:
    """从脚本位置向上查找同时包含 Ground/ 与 Airborne/ 的组合父目录。"""
    for candidate in Path(__file__).resolve().parents:
        if (candidate / GROUND_PROTO).is_file() and any(
            (candidate / proto).is_file() for proto in AIRBORNE_PROTOS
        ):
            return candidate
    return None


def normalize_newlines(text: str) -> str:
    """统一行尾为 LF，消除跨平台检出带来的 CRLF/LF 差异。"""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="校验 messages.proto 三份副本一致性")
    parser.add_argument("--quiet", action="store_true", help="仅在不一致时输出")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="找不到 Airborne 副本时也判失败（默认优雅跳过）",
    )
    args = parser.parse_args()

    combined_root = find_combined_root()
    if combined_root is None:
        message = (
            "未找到同时包含 Ground/ 与 Airborne/ 的组合父目录，"
            "无法进行跨仓库 proto 校验（可能只检出了单个仓库）。"
        )
        if args.strict:
            print(f"错误: {message}", file=sys.stderr)
            return 2
        if not args.quiet:
            print(f"跳过: {message}")
        return 0

    baseline = normalize_newlines((combined_root / GROUND_PROTO).read_text(encoding="utf-8"))

    checked = 1  # 基准本身算一份
    mismatches: list[Path] = []
    for other_path in AIRBORNE_PROTOS:
        absolute = combined_root / other_path
        if not absolute.is_file():
            # 组合父目录存在但某份副本缺失，属于结构异常，始终报错。
            print(f"错误: 找不到 proto 副本: {absolute}", file=sys.stderr)
            return 2
        checked += 1
        other = normalize_newlines(absolute.read_text(encoding="utf-8"))
        if other != baseline:
            mismatches.append(other_path)
            print(f"不一致: {other_path} 与基准 {GROUND_PROTO} 不同", file=sys.stderr)
            diff = difflib.unified_diff(
                baseline.splitlines(keepends=True),
                other.splitlines(keepends=True),
                fromfile=str(GROUND_PROTO),
                tofile=str(other_path),
            )
            sys.stderr.writelines(diff)
            print("", file=sys.stderr)

    if mismatches:
        print(
            f"\n校验失败: {len(mismatches)} 份 proto 副本与基准不一致。"
            f"修改 messages.proto 时必须同步全部 {checked} 份副本。",
            file=sys.stderr,
        )
        return 1

    if not args.quiet:
        print(f"校验通过: {checked} 份 messages.proto 副本内容一致（根目录: {combined_root}）。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
