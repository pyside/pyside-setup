# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import sys
import logging
import argparse
import stat
import warnings
import shutil
from dataclasses import dataclass

from pathlib import Path
from jinja2 import Environment, FileSystemLoader

from android_utilities import (run_command, download_android_commandlinetools,
                               download_android_ndk, install_android_packages,
                               download_prebuilt_python_android,
                               resolve_target_python_version,
                               ANDROID_NDK_VERSION,
                               MIN_ANDROID_API_LEVEL,
                               DEFAULT_ANDROID_API_LEVEL,
                               SUPPORTED_ANDROID_PLATFORMS)

# Android ABI data per supported platform:
# android_abi, qt_plat_name, gcc_march, plat_bits
_PLATFORM_DATA = {
    "aarch64": ("arm64-v8a", "arm64_v8a", "armv8-a", "64"),
    "x86_64": ("x86_64", "x86_64", "x86-64", "64"),
}

SKIP_UPDATE_HELP = ("skip the updation of SDK packages build-tools, platform-tools to"
                    " latest version")

ACCEPT_LICENSE_HELP = ('''
Accepts license automatically for Android SDK installation. Otherwise,
accept the license manually through command line.
''')

CLEAN_CACHE_HELP = ('''
Cleans cache stored in $HOME/.pyside6_android_deploy.
Options:

1. all - all the cache including Android Ndk, Android Sdk and the downloaded Python are deleted.
2. ndk - Only the Android Ndk is deleted.
3. sdk - Only the Android Sdk is deleted.
4. python - The downloaded prebuilt Python for all platforms is deleted.
5. toolchain - The CMake toolchain file required for cross-compiling Qt for Python, for all
               platforms are deleted.

If --clean-cache is used and no explicit value is suppied, then `all` is used as default.
''')

COIN_RUN_HELP = ('''
When run by Qt's continuos integration system COIN. This option is irrelevant to user building
their own wheels.
''')


@dataclass
class PlatformData:
    plat_name: str
    api_level: str
    android_abi: str
    qt_plat_name: str
    gcc_march: str
    plat_bits: str


def download_only_exists():
    '''
    check if '--download-only' exists in command line arguments
    '''
    return "--download-only" in sys.argv


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="This tool cross builds CPython for Android and uses that Python to cross build"
                    "Android Qt for Python wheels",
        formatter_class=argparse.RawTextHelpFormatter,
    )

    parser.add_argument("-p", "--plat-name", type=str, nargs="*",
                        choices=SUPPORTED_ANDROID_PLATFORMS,
                        default=SUPPORTED_ANDROID_PLATFORMS,
                        dest="plat_names",
                        help="Android target platforms")

    parser.add_argument("-v", "--verbose", help="run in verbose mode", action="store_const",
                        dest="loglevel", const=logging.INFO)
    # As opposed to Qt, Qt for Python does not require API level 28 because it can be built with a
    # higher API for toolchain compatibility, while still remaining compatible with Qt's runtime
    # minimum.
    parser.add_argument("--api-level", type=str,
                        default=DEFAULT_ANDROID_API_LEVEL,
                        help="Minimum Android API level to use")
    parser.add_argument("--ndk-path", type=str,
                        help="Path to an existing Android NDK. Omit to download "
                             f"r{ANDROID_NDK_VERSION}, which is the version "
                             "this tool is tested against")
    # sdk path is needed to compile all the Qt Java Acitivity files into Qt6AndroidBindings.jar
    parser.add_argument("--sdk-path", type=str, help="Path to Android SDK")
    parser.add_argument(
        "--qt-install-path",
        type=str,
        required=not download_only_exists(),
        help="Qt installation path eg: /home/Qt/6.8.0"
    )

    parser.add_argument("--dry-run", action="store_true", help="show the commands to be run")

    parser.add_argument("--skip-update", action="store_true",
                        help=SKIP_UPDATE_HELP)

    parser.add_argument("--auto-accept-license", action="store_true",
                        help=ACCEPT_LICENSE_HELP)

    parser.add_argument("--clean-cache", type=str, nargs="?", const="all",
                        choices=["all", "python", "ndk", "sdk", "toolchain"],
                        help=CLEAN_CACHE_HELP)

    parser.add_argument("--coin", action="store_true",
                        help=COIN_RUN_HELP)

    parser.add_argument("--download-only", action="store_true",
                        help="Only download Android NDK and SDK")

    parser.add_argument("--reuse-build", action="store_true",
                        help="Reuse the existing build directories and only rebuild "
                             "changed sources")

    parser.add_argument("--sbom", action="store_true",
                        help="Generate SBOM files")

    args = parser.parse_args()

    logging.basicConfig(level=args.loglevel)
    pyside_setup_dir = Path(__file__).parents[2].resolve()
    # Android wheels go into their own directory. dist/ is owned by the
    # desktop build, which deletes it wholesale before repopulating it, and
    # that would take the cross-compiled wheels with it.
    android_dist_dir = pyside_setup_dir / "dist_android"
    qt_install_path = args.qt_install_path
    ndk_path = args.ndk_path
    sdk_path = args.sdk_path
    android_abi = None
    gcc_march = None
    plat_bits = None
    dry_run = args.dry_run
    plat_names = args.plat_names
    api_level = args.api_level
    skip_update = args.skip_update
    auto_accept_license = args.auto_accept_license
    clean_cache = args.clean_cache
    coin = args.coin
    download_only = args.download_only
    reuse_build = args.reuse_build
    sbom = args.sbom

    # auto download Android NDK and SDK
    pyside6_deploy_cache = Path.home() / ".pyside6_android_deploy"
    logging.info(f"Cache created at {str(pyside6_deploy_cache.resolve())}")
    pyside6_deploy_cache.mkdir(exist_ok=True)

    if pyside6_deploy_cache.exists() and clean_cache:
        if clean_cache == "all":
            shutil.rmtree(pyside6_deploy_cache)
        elif clean_cache == "ndk":
            cached_ndk_dir = pyside6_deploy_cache / "android-ndk"
            if cached_ndk_dir.exists():
                shutil.rmtree(cached_ndk_dir)
        elif clean_cache == "sdk":
            cached_sdk_dir = pyside6_deploy_cache / "android-sdk"
            if cached_sdk_dir.exists():
                shutil.rmtree(cached_sdk_dir)
        elif clean_cache == "python":
            for cc_python_path in pyside6_deploy_cache.glob("Python-*"):
                if cc_python_path.is_dir():
                    shutil.rmtree(cc_python_path)
        elif clean_cache == "toolchain":
            for toolchain_path in pyside6_deploy_cache.glob("toolchain_*"):
                if toolchain_path.is_file():
                    toolchain_path.unlink()

    if not ndk_path:
        # Download android ndk
        ndk_path = download_android_ndk(pyside6_deploy_cache)

    if not sdk_path:
        # download and unzip command-line tools
        sdk_path = download_android_commandlinetools(pyside6_deploy_cache)
        # install and update required android packages
        install_android_packages(android_sdk_dir=sdk_path, android_api=api_level,
                                 dry_run=dry_run, accept_license=auto_accept_license,
                                 skip_update=skip_update)

    templates_path = Path(__file__).parent / "templates"
    environment = Environment(loader=FileSystemLoader(templates_path))

    target_python_version, target_python_full_version = resolve_target_python_version()

    for plat_name in plat_names:
        android_abi, qt_plat_name, gcc_march, plat_bits = _PLATFORM_DATA[plat_name]
        platform_data = PlatformData(plat_name, api_level, android_abi,
                                     qt_plat_name, gcc_march, plat_bits)

        # python path is valid, if Python for android installation exists in python_path
        python_path = (pyside6_deploy_cache
                       / f"Python-{target_python_version}"
                         f"-{platform_data.plat_name}-linux-android"
                       / "_install")
        valid_python_path = python_path.exists()
        if Path(python_path).exists():
            expected_dirs = ["lib", "include"]
            for expected_dir in expected_dirs:
                if not (Path(python_path) / expected_dir).is_dir():
                    valid_python_path = False
                    warnings.warn(
                        f"{str(python_path.resolve())} is corrupted. New Python for {plat_name} "
                        f"android will be downloaded into {str(pyside6_deploy_cache.resolve())}"
                    )
                    break

        if not valid_python_path:
            download_prebuilt_python_android(
                full_version=target_python_full_version,
                plat_name=platform_data.plat_name,
                install_path=python_path)

        if download_only:
            continue

        qfp_toolchain = pyside6_deploy_cache / f"toolchain_{platform_data.plat_name}.cmake"

        template = environment.get_template("toolchain_default.tmpl.cmake")
        content = template.render(
            ndk_path=ndk_path,
            sdk_path=sdk_path,
            api_level=platform_data.api_level,
            qt_install_path=qt_install_path,
            plat_name=platform_data.plat_name,
            android_abi=platform_data.android_abi,
            qt_plat_name=platform_data.qt_plat_name,
            gcc_march=platform_data.gcc_march,
            plat_bits=platform_data.plat_bits,
            python_version=target_python_version,
            target_python_path=python_path,
            min_android_api=MIN_ANDROID_API_LEVEL
        )

        # Render first and compare, because a cached toolchain file is only
        # valid for the inputs it was generated from. The Python version, NDK,
        # SDK, Qt path and API level can all differ from the previous run, and
        # reusing the file merely because it exists silently cross-compiles
        # against the old ones.
        cached_content = (qfp_toolchain.read_text(encoding="utf-8")
                          if qfp_toolchain.exists() else None)

        if cached_content != content:
            logging.info(f"Writing Qt for Python toolchain file into {qfp_toolchain}")
            with open(qfp_toolchain, mode="w", encoding="utf-8") as ccompile_script:
                ccompile_script.write(content)

            # give run permission to cross compile script
            qfp_toolchain.chmod(qfp_toolchain.stat().st_mode | stat.S_IEXEC)
        else:
            logging.info(f"Reusing Qt for Python toolchain file {qfp_toolchain}")

        if sys.platform == "linux":
            host_qt_install_suffix = "gcc_64"
        elif sys.platform == "darwin":
            host_qt_install_suffix = "macos"
        else:
            raise RuntimeError("Qt for Python cross compilation not supported on this platform")

        if coin:
            target_path = str(Path(qt_install_path) / "target")
            qt_host_install_path = qt_install_path
            # Qt CI release automation is expecting to find the wheels from dist directory
            android_dist_dir = pyside_setup_dir / "dist"
        else:
            target_path = str(Path(qt_install_path) / f"android_{platform_data.qt_plat_name}")
            qt_host_install_path = str(Path(qt_install_path) / host_qt_install_suffix)

        # run the cross compile script
        logging.info(f"Running Qt for Python cross-compile for platform {platform_data.plat_name}")
        # --limited-api=yes is passed explicitly so that bdist_wheel tags the
        # wheel abi3. CMake already builds against the limited API by default,
        # but setup.py only knows about it when the option is given, and would
        # otherwise stamp the target interpreter version into the wheel name.
        qfp_ccompile_cmd = [sys.executable, "setup.py", "bdist_wheel", "--parallel=9",
                            "--standalone",
                            f"--cmake-toolchain-file={str(qfp_toolchain.resolve())}",
                            f"--qt-host-path={qt_host_install_path}",
                            f"--plat-name=android_{platform_data.plat_name}",
                            f"--python-target-path={python_path}",
                            f"--qt-target-path={target_path}",
                            f"--dist-dir={str(android_dist_dir)}",
                            "--limited-api=yes",
                            "--no-qt-tools"]

        if reuse_build:
            qfp_ccompile_cmd.append("--reuse-build")

        if sbom:
            qfp_ccompile_cmd.append("--sbom")

        run_command(qfp_ccompile_cmd, cwd=pyside_setup_dir, dry_run=dry_run, show_stdout=True)

    if download_only:
        print(f"Android NDK, SDK and Python downloaded successfully into "
              f"{pyside6_deploy_cache}")
        sys.exit(0)
