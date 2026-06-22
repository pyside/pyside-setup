# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import logging
import argparse
import sys
import os
import subprocess
from pathlib import Path

from ios_utilities import (generate_toolchain,
                           python_xcframework_slice_dir,
                           PYSIDE_SETUP_ROOT)
from python_xcframework import download_python_support

COIN_RUN_HELP = ('''
When run by Qt's continuos integration system COIN. This option is irrelevant to user building
their own wheels.
''')


def cross_compile(args: argparse.Namespace) -> None:
    """Cross compile PySide6 for iOS"""
    simulator = args.simulator or (args.arch == "x86_64")
    arch = args.arch
    coin = args.coin
    qt_install_path = args.qt_install_path.expanduser().resolve()
    dry_run = args.dry_run
    # iOS wheels go into their own directory. dist/ is owned by the
    # desktop build, which deletes it wholesale before repopulating it, and
    # that would take the cross-compiled wheels with it.
    ios_dist_dir = PYSIDE_SETUP_ROOT / "dist_ios"

    if coin:
        qt_ios = qt_install_path / "target"
        qt_macos = qt_install_path
        ios_dist_dir = PYSIDE_SETUP_ROOT / "dist"
    else:
        qt_ios = qt_install_path / (f"ios_simulator_{arch}" if simulator else "ios_device")
        qt_macos = qt_install_path / "macos"

    # Download the official python.org Python.xcframework
    python_xcframework = download_python_support(dry_run=dry_run)

    # Generate toolchain file
    toolchain = generate_toolchain(
        arch=arch,
        simulator=simulator,
        python_xcframework=python_xcframework,
        qt_ios=qt_ios,
        dry_run=dry_run,
    )

    # Cross-compile PySide6
    suffix = f"{arch}_simulator" if simulator else arch
    plat_name = f"ios_{suffix}"
    python_slice = python_xcframework / python_xcframework_slice_dir(arch, simulator)
    cmd = [
        sys.executable, "setup.py", "bdist_wheel",
        "--standalone",
        f"--cmake-toolchain-file={toolchain}",
        f"--qt-host-path={qt_macos}",
        f"--qt-target-path={qt_ios}",
        f"--python-target-path={python_slice}",
        f"--plat-name={plat_name}",
        f"--dist-dir={str(ios_dist_dir)}",
        "--no-qt-tools",
    ]

    if args.reuse_build:
        cmd.append("--reuse-build")

    if args.sbom:
        cmd.append("--sbom")

    if dry_run:
        print(" ".join(cmd))
        return

    env = os.environ.copy()
    logging.info(f"Running bdist_wheel for platform {plat_name}")
    subprocess.run(cmd, cwd=PYSIDE_SETUP_ROOT, env=env, check=True)


def main():
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(
        description="PySide6 iOS cross-compilation tools",
        formatter_class=argparse.RawTextHelpFormatter,
    )

    parser.add_argument(
        "--qt-install-path", required=True, type=Path,
        help="Qt installation root, e.g. ~/Qt/6.12.0",
    )
    parser.add_argument(
        "--arch", choices=["arm64", "x86_64"], default="arm64",
        help="Target CPU architecture (default: arm64; x86_64 implies --simulator)",
    )
    parser.add_argument(
        "--simulator", action="store_true",
        help="Build for iOS Simulator (device is the default)",
    )
    parser.add_argument(
        "--coin", action="store_true",
        help=COIN_RUN_HELP,
    )

    parser.add_argument("--dry-run", action="store_true", help="show the commands to be run")

    parser.add_argument("--reuse-build", action="store_true",
                        help="Reuse the existing build directories and only rebuild "
                             "changed sources")

    parser.add_argument("--sbom", action="store_true",
                        help="Generate SBOM files")

    args = parser.parse_args()

    cross_compile(args)


if __name__ == "__main__":
    main()
