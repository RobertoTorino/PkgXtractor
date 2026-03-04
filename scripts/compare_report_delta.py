#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

KEYS = [
    "left_file_count",
    "right_file_count",
    "common_file_count",
    "only_left_count",
    "only_right_count",
    "size_mismatch_count",
    "hash_mismatch_count",
]


def load_report(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def summarize_delta(before: dict, after: dict) -> str:
    lines = []
    lines.append("=== Compare Report Delta ===")
    for key in KEYS:
        b = int(before.get(key, 0))
        a = int(after.get(key, 0))
        d = a - b
        trend = "UNCHANGED"
        if d < 0:
            trend = "DOWN"
        elif d > 0:
            trend = "UP"
        lines.append(f"{key}: before={b}, after={a}, delta={d:+d} ({trend})")

    for key in ["only_left", "only_right", "size_mismatch", "hash_mismatch"]:
        bset = set(before.get(key, []))
        aset = set(after.get(key, []))
        fixed = sorted(bset - aset)
        new = sorted(aset - bset)
        lines.append("")
        lines.append(f"[{key}] fixed={len(fixed)}, new={len(new)}")
        if fixed:
            lines.append("  fixed (first 20):")
            lines.extend(f"    - {x}" for x in fixed[:20])
        if new:
            lines.append("  new (first 20):")
            lines.extend(f"    - {x}" for x in new[:20])

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two compare_report JSON files (before vs after).")
    parser.add_argument("before", type=Path, help="Path to before report JSON")
    parser.add_argument("after", type=Path, help="Path to after report JSON")
    parser.add_argument("--out", type=Path, default=None, help="Optional output txt file")
    args = parser.parse_args()

    before = load_report(args.before)
    after = load_report(args.after)

    text = summarize_delta(before, after)
    print(text)

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
        print(f"\nDelta written to: {args.out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
