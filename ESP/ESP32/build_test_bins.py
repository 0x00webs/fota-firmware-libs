#!/usr/bin/env python3
"""
build_test_bins.py
──────────────────
Builds firmware_v1 … firmware_v5 with PlatformIO and copies the resulting
.bin files to tests/ with versioned names (firmware_v1.0.0.bin, etc.).

Usage:
    python build_test_bins.py           # build + copy all versions
    python build_test_bins.py 2 3       # build + copy only v2 and v3
    python build_test_bins.py --copy-only   # skip build, just re-copy existing bins
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
TESTS_DIR = ROOT / "tests"

VERSIONS = {
    1: "1.0.0",
    2: "2.0.0",
    3: "3.0.0",
    4: "4.0.0",
    5: "5.0.0",
}


def build(env: str) -> bool:
    """Run `pio run -e <env>`.  Returns True on success."""
    cmd = ["pio", "run", "--environment", env]
    print(f"\n{'─'*60}")
    print(f"  Building: {env}")
    print(f"{'─'*60}")
    result = subprocess.run(cmd, cwd=ROOT)
    return result.returncode == 0


def copy_bin(n: int, version: str) -> Path:
    """Copy .pio/build/firmware_vN/firmware.bin → tests/firmware_vN.0.0.bin"""
    src = ROOT / ".pio" / "build" / f"firmware_v{n}" / "firmware.bin"
    if not src.exists():
        raise FileNotFoundError(f"Binary not found: {src}")
    TESTS_DIR.mkdir(exist_ok=True)
    dst = TESTS_DIR / f"firmware_v{version}.bin"
    shutil.copy2(src, dst)
    size_kb = dst.stat().st_size / 1024
    print(f"  Copied: tests/{dst.name}  ({size_kb:.1f} KB)")
    return dst


def main():
    parser = argparse.ArgumentParser(description="Build and collect FOTA test binaries.")
    parser.add_argument(
        "versions",
        nargs="*",
        type=int,
        choices=list(VERSIONS.keys()),
        metavar="N",
        help="Version numbers to build (1-5). Defaults to all.",
    )
    parser.add_argument(
        "--copy-only",
        action="store_true",
        help="Skip build, only copy pre-existing .bin files.",
    )
    args = parser.parse_args()

    targets = args.versions if args.versions else list(VERSIONS.keys())

    failed = []
    copied = []

    for n in targets:
        version = VERSIONS[n]
        env = f"firmware_v{n}"

        if not args.copy_only:
            ok = build(env)
            if not ok:
                print(f"\n  [ERROR] Build failed for {env} — skipping copy.")
                failed.append(env)
                continue

        try:
            dst = copy_bin(n, version)
            copied.append(dst.name)
        except FileNotFoundError as e:
            print(f"\n  [ERROR] {e}")
            failed.append(env)

    # ── Summary ──────────────────────────────────────────────────────────────
    print(f"\n{'═'*60}")
    if copied:
        print(f"  Done.  {len(copied)} bin(s) in tests/:")
        for name in copied:
            print(f"    • {name}")
    if failed:
        print(f"\n  Failed ({len(failed)}): {', '.join(failed)}")
        sys.exit(1)
    print(f"{'═'*60}\n")


if __name__ == "__main__":
    main()
