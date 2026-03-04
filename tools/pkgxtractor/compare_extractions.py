#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
from typing import Dict, Tuple


def build_index(root: Path, hash_files: bool) -> Dict[str, Dict[str, str]]:
    index: Dict[str, Dict[str, str]] = {}
    for path in root.rglob("*"):
        if path.is_dir():
            continue
        rel = path.relative_to(root).as_posix()
        entry = {"size": str(path.stat().st_size)}
        if hash_files:
            h = hashlib.sha256()
            with path.open("rb") as f:
                for chunk in iter(lambda: f.read(1024 * 1024), b""):
                    h.update(chunk)
            entry["sha256"] = h.hexdigest()
        index[rel] = entry
    return index


def parse_filelist(filelist: Path) -> Dict[str, Tuple[str, str]]:
    result: Dict[str, Tuple[str, str]] = {}
    with filelist.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "Name: \"" not in line:
                continue
            try:
                name_part = line.split("Name: \"", 1)[1]
                name = name_part.split("\"", 1)[0]
                inode_part = line.split("Inode:", 1)[1]
                inode = inode_part.split(" ", 1)[0]
                type_part = line.split("Type:", 1)[1]
                ftype = type_part.split(" ", 1)[0]
                key = f"inode={inode};type={ftype};name={name}"
                result[key] = (inode, ftype)
            except Exception:
                continue
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two extracted PKG outputs quickly.")
    parser.add_argument("left", type=Path, help="First extraction folder (e.g. PkgXtractor output)")
    parser.add_argument("right", type=Path, help="Second extraction folder (e.g. LibOrbisPkg output)")
    parser.add_argument("--hash", action="store_true", help="Also compare SHA-256 for common files")
    parser.add_argument("--left-filelist", type=Path, default=None, help="Optional filelist.txt from left extractor")
    parser.add_argument("--right-filelist", type=Path, default=None, help="Optional filelist.txt from right extractor")
    parser.add_argument("--json-out", type=Path, default=None, help="Optional JSON report output path")
    args = parser.parse_args()

    left = args.left.resolve()
    right = args.right.resolve()

    if not left.exists() or not left.is_dir():
        raise SystemExit(f"Left folder not found: {left}")
    if not right.exists() or not right.is_dir():
        raise SystemExit(f"Right folder not found: {right}")

    print(f"Indexing left:  {left}")
    left_index = build_index(left, args.hash)
    print(f"Indexing right: {right}")
    right_index = build_index(right, args.hash)

    left_paths = set(left_index.keys())
    right_paths = set(right_index.keys())

    only_left = sorted(left_paths - right_paths)
    only_right = sorted(right_paths - left_paths)
    common = sorted(left_paths & right_paths)

    # Calculate total sizes
    left_total_size = sum(int(left_index[p]["size"]) for p in left_paths)
    right_total_size = sum(int(right_index[p]["size"]) for p in right_paths)
    common_total_size = sum(int(left_index[p]["size"]) for p in common)

    size_mismatch = []
    hash_mismatch = []

    for rel in common:
        if left_index[rel]["size"] != right_index[rel]["size"]:
            size_mismatch.append(rel)
            continue
        if args.hash:
            if left_index[rel].get("sha256") != right_index[rel].get("sha256"):
                hash_mismatch.append(rel)

    filelist_summary = {}
    if args.left_filelist and args.right_filelist:
        left_fl = parse_filelist(args.left_filelist)
        right_fl = parse_filelist(args.right_filelist)
        left_fl_keys = set(left_fl.keys())
        right_fl_keys = set(right_fl.keys())
        filelist_summary = {
            "left_only_entries": sorted(left_fl_keys - right_fl_keys),
            "right_only_entries": sorted(right_fl_keys - left_fl_keys),
            "common_entries": len(left_fl_keys & right_fl_keys),
        }

    report = {
        "left": str(left),
        "right": str(right),
        "left_file_count": len(left_index),
        "right_file_count": len(right_index),
        "common_file_count": len(common),
        "left_total_size": left_total_size,
        "right_total_size": right_total_size,
        "common_total_size": common_total_size,
        "only_left_count": len(only_left),
        "only_right_count": len(only_right),
        "size_mismatch_count": len(size_mismatch),
        "hash_mismatch_count": len(hash_mismatch),
        "only_left": only_left,
        "only_right": only_right,
        "size_mismatch": size_mismatch,
        "hash_mismatch": hash_mismatch,
        "filelist_summary": filelist_summary,
    }

    print("\n=== Comparison Summary ===")
    print(f"Left files:           {report['left_file_count']}")
    print(f"Right files:          {report['right_file_count']}")
    print(f"Common files:         {report['common_file_count']}")
    print(f"Left total size:      {report['left_total_size']:,} bytes ({report['left_total_size'] / (1024**3):.2f} GB)")
    print(f"Right total size:     {report['right_total_size']:,} bytes ({report['right_total_size'] / (1024**3):.2f} GB)")
    print(f"Common total size:    {report['common_total_size']:,} bytes ({report['common_total_size'] / (1024**3):.2f} GB)")
    print(f"Only in left:         {report['only_left_count']}")
    print(f"Only in right:        {report['only_right_count']}")
    print(f"Size mismatches:      {report['size_mismatch_count']}")
    if args.hash:
        print(f"Hash mismatches:      {report['hash_mismatch_count']}")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        with args.json_out.open("w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        print(f"\nJSON report written: {args.json_out}")

    preview_count = 20
    if only_left:
        print("\n-- Only in left (first 20) --")
        for p in only_left[:preview_count]:
            print(p)
    if only_right:
        print("\n-- Only in right (first 20) --")
        for p in only_right[:preview_count]:
            print(p)
    if size_mismatch:
        print("\n-- Size mismatch (first 20) --")
        for p in size_mismatch[:preview_count]:
            print(p)
    if args.hash and hash_mismatch:
        print("\n-- Hash mismatch (first 20) --")
        for p in hash_mismatch[:preview_count]:
            print(p)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
