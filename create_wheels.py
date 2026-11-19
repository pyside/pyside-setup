# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import functools
import os
import platform
import re
import sys
from fnmatch import fnmatch
import importlib.util
import inspect
import json
import logging
from argparse import ArgumentParser, Namespace
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
from shutil import copy, rmtree, copytree

import build  # type: ignore
import pyproject_hooks
from setuptools.command.egg_info import FileList
import build_scripts.wheel_files
from build_scripts.wheel_files import (ModuleData,  # type: ignore
                                       set_pyside_package_path,
                                       wheel_files_pyside_addons,
                                       wheel_files_pyside_essentials,
                                       wheel_files_pyside_webengine,
                                       wheel_files_pyside_pdf)
from build_scripts import PYSIDE_WINDOWS_BIN_TOOLS
from build_scripts.log import log
from build_scripts.utils import available_pyside_tools, parse_cmake_conf_assignments_by_key


PACKAGE_FOR_WHEELS = "package_for_wheels"
PYSIDE_DESCRIPTION = "Python bindings for the Qt cross-platform application and UI framework"

# Wheels whose content is tied to the Chromium version Qt WebEngine is built
# against. Their version carries it as an extra component of the release
# segment, e.g. '6.12.0.140' instead of '6.12.0', so that it is visible on
# PyPI which Chromium a given wheel ships.
CHROMIUM_VERSIONED_WHEELS = ("PySide6_WebEngine", "PySide6_Pdf")

# Maps each wheel to the other wheels it depends on, version-pinned to the same
# release. Wheels like shiboken6 and shiboken6_generator have no
# dependencies, add for completeness.
WHEEL_DEPENDENCIES: dict[str, list[str]] = {
    "PySide6": ["PySide6_Essentials", "PySide6_Addons", "PySide6_WebEngine",
                "PySide6_Pdf"],
    "PySide6_Examples": ["PySide6_Essentials", "PySide6_Addons", "PySide6_WebEngine",
                         "PySide6_Pdf"],
    "PySide6_WebEngine": ["PySide6_Addons"],
    "PySide6_Pdf": ["PySide6_Essentials"],
    "PySide6_Addons": ["PySide6_Essentials"],
    "PySide6_Essentials": ["shiboken6"],
    "shiboken6": [],
    "shiboken6_generator": [],
}

# Maps a wheel to optional ('extras') dependencies, version-pinned to the
# same release, that are not required for the wheel to work but unlock
# additional functionality when installed, e.g.:
#   pip install PySide6-Addons[webengine]
#   pip install PySide6[full]
#
# Each extra maps to (dependencies, platforms), where 'platforms' restricts
# it to specific sys.platform values, or None to apply on every platform.
#
# On Linux, QtWebView (packaged in PySide6_Addons) only ships the
# 'webengine' backend plugin (there is no native OS web view like on
# Android/iOS/Windows/macOS), so it is unusable there without
# PySide6_WebEngine installed; elsewhere it would be a no-op, so it's not
# advertised there.
WHEEL_OPTIONAL_DEPENDENCIES: dict[str, dict[str, tuple[list[str], tuple[str, ...] | None]]] = {
    "PySide6_Addons": {
        "webengine": (["PySide6_WebEngine"], ("linux",)),
    },
    "PySide6": {
        "full": (["PySide6_Examples", "shiboken6_generator"], None),
    },
}


class BuildType(Enum):
    LIMITED_API = auto()
    FREE_THREADED = auto()
    DEBUG = auto()
    RELEASE = auto()


BUILDTYPE_POSTFIX_MAPPING = [(BuildType.LIMITED_API, "a"), (BuildType.FREE_THREADED, "t"),
                             (BuildType.DEBUG, "d"), (BuildType.RELEASE, "")]


@dataclass
class SetupData:
    name: str
    version: str
    description: str
    readme: str
    console_scripts: list[str]


@functools.cache
def get_version_from_package(name: str, package_path: Path) -> str:
    # Get version from the already configured '__init__.py' file
    init_file = package_path / name / "__init__.py"
    version = ""
    with open(init_file, encoding="utf-8") as f:
        for line in f:
            if line.strip().startswith("__version__"):
                version = line.split("=")[1].strip().replace('"', "")
                break
    if not version:
        raise ValueError(f"Unable to find '__version__' in {init_file}")
    return version


@functools.cache
def get_chromium_version() -> str:
    """Return the Chromium version Qt WebEngine is built against, as set in
    'sources/pyside6/.cmake.conf'."""
    pyside_project_dir = Path(__file__).resolve().parent / "sources" / "pyside6"
    version = parse_cmake_conf_assignments_by_key(pyside_project_dir).get(
        "pyside_CHROMIUM_VERSION")
    if not version:
        raise ValueError("Unable to find 'pyside_CHROMIUM_VERSION' in "
                         f"{pyside_project_dir / '.cmake.conf'}")
    return version


def add_chromium_version(version: str, chromium_version: str) -> str:
    """Append the Chromium version to the release segment of a PEP 440
    version, keeping any pre/post/dev/local suffix in place, e.g.
    '6.12.0a1' becomes '6.12.0.140a1'."""
    release = re.match(r"\d+(\.\d+)*", version)
    if not release:
        raise ValueError(f"Unable to find the release segment of '{version}'")
    return f"{release.group()}.{chromium_version}{version[release.end():]}"


def get_wheel_version(wheel_name: str, package_path: Path) -> str:
    """Return the version the given wheel is released with."""
    # Every 'PySide6*' wheel is built out of the 'PySide6' package directory,
    # so they all share its version.
    package = wheel_name if wheel_name.startswith("shiboken6") else "PySide6"
    version = get_version_from_package(package, package_path)
    if wheel_name in CHROMIUM_VERSIONED_WHEELS:
        version = add_chromium_version(version, get_chromium_version())
    return version


def create_module_plugin_json(wheel_name: str, data: list[ModuleData], package_path: Path):
    all_plugins = {}

    for module in data:
        all_plugins[module.name] = module.plugins

    # write the dictionary modules->plugins dictionary to a .json file and include this .json file
    # This file is picked up by the deployment tool to figure out the plugin dependencies
    # of a PySide6 application
    if all_plugins:
        with open(f"{package_path}/PySide6/{wheel_name}.json", 'w', encoding="utf-8") as fp:
            json.dump(all_plugins, fp, indent=4)


def get_manifest(wheel_name: str, data: list[ModuleData], package_path: Path, verbose: int) -> str:
    lines = []

    for module in data:
        # It's crucial to have this adjust method here
        # because it include all the necessary modifications to make
        # our solution work on the three main platforms.
        module.adjusts_paths_and_extensions()

        for field in module.get_fields():
            if field == "name":
                lines.append(f"# {getattr(module, field)}")
                continue
            if field == "ext":
                continue
            for line in getattr(module, field):
                file = f"PySide6/{line}"
                if verbose > 0 and "*" not in file and not (package_path / file).exists():
                    log.warning(f"{file} does not exist.")
                if field in ("extra_dirs", "qml", "plugins"):
                    lines.append(f"graft {file}")
                elif field == "qtlib" and sys.platform == "darwin":
                    lines.append(f"graft {file}")
                else:
                    lines.append(f"include {file}")
        lines.append("recursive-exclude PySide6 *qt.conf*")
        lines.append("")

    # Skip certain files if needed
    lines.append("recursive-exclude PySide6/Qt/qml *.debug")
    lines.append("prune PySide6/Qt/qml/QtQuick3D/MaterialEditor")

    # adding PySide6_Essentials.json and PySide6_Addons.json
    lines.append(f"include PySide6/{wheel_name}.json")

    # Only include CMake configs for PySide6_Essentials
    if wheel_name == "PySide6_Essentials":
        lines.append("graft PySide6/lib/cmake")

    return "\n".join(lines)


def get_simple_manifest(name: str) -> str:
    if name == "PySide6":
        return f"prune {name}\n"
    elif name == "PySide6_Examples":
        return "prune PySide6\ngraft PySide6/examples\n"
    return f"graft {name}\n"


def get_platform_tag(package_path: Path) -> str:
    _os = sys.platform
    arch = platform.machine()

    # as Qt6 we know it's GLIBC 2.28 on RHEL 8.4
    _tag = ""
    if _os == "linux":
        glibc = platform.libc_ver()[1].replace(".", "_")
        # Will generate manylinux_2_28_x86_64
        _tag = f"manylinux_{glibc}_{arch}"
    elif _os == "darwin":
        # find _config.py and load it to obtain __qt_macos_min_deployment_target__
        target = None
        config_py = package_path / "shiboken6" / "_config.py"
        if not config_py.exists():
            raise RuntimeError(f"Unable to find {config_py}")

        module_name = config_py.name[:-3]
        _spec = importlib.util.spec_from_file_location(f"{module_name}", config_py)
        if _spec is None:
            raise RuntimeError(f"Unable to create ModuleSpec from {config_py}")
        _module = importlib.util.module_from_spec(_spec)
        if _spec.loader is None:
            raise RuntimeError(f"ModuleSpec for {module_name} has no valid loader.")
        _spec.loader.exec_module(module=_module)
        target = _module.__qt_macos_min_deployment_target__

        if not target:
            log.error("Couldn't get the value from MACOSX_DEPLOYMENT_TARGET. "
                      "Falling back to local platform version.")
            mac_ver, _, _ = platform.mac_ver()
            # We get 10.14.2 for example, and transform into 10_14
            target = "_".join(mac_ver.split(".")[:2])
        else:
            version = target.split(".")
            target = f"{version[0]}_0"
        # TODO: Make it general
        # To check if is compatible with 64bit on multi-arch systems
        # is_64bits = sys.maxsize > 2**32
        #
        # We know the CI builds universal2 wheels
        _tag = f"macosx_{target}_universal2"
    elif _os == "win32":
        win_arch = platform.machine()
        msvc_arch = "arm64" if win_arch.startswith("ARM64") else "amd64"
        _tag = f"win_{msvc_arch}"

    return _tag


def get_conf_python_minor_version_range() -> tuple[int, int]:
    """Return the supported minor_version range as specified in config file."""
    config_file = Path("wheel_artifacts/python_versions.json")
    with config_file.open(encoding="utf-8") as json_file:
        json_data = json.load(json_file)
    return json_data["minimum_minor_version"], json_data["maximum_minor_version"]


def python_minor_version_range(build_type: BuildType) -> tuple[int, int]:
    """Return the supported minor_version range."""
    min_minor_version, max_minor_version = get_conf_python_minor_version_range()
    return min_minor_version, max_minor_version


def python_language_versions(minor_versions: tuple[int, int]) -> str:
    """Return the Programming Language version entries as a TOML list."""
    PYTHON = "Programming Language :: Python"
    result = f'"{PYTHON}",\n    "{PYTHON} :: 3",\n'
    for minor in range(minor_versions[0], minor_versions[1] + 1):
        result += f'    "{PYTHON} :: 3.{minor}",\n'
    return result


def generate_pyproject_toml(artifacts: Path, setup: SetupData, package_path: Path,
                            build_type: BuildType, packages: list[str]) -> str:
    _name = setup.name
    _tag = get_platform_tag(package_path)

    _console_scripts = ""
    if setup.console_scripts:
        _formatted_console_scripts = "\n".join(setup.console_scripts)
        _console_scripts = f"[project.scripts]\n{_formatted_console_scripts}"

    # Installing dependencies. The pinned version is resolved per dependency
    # because the wheels in 'CHROMIUM_VERSIONED_WHEELS' are released with a
    # different version than the rest.
    _dependencies = [f"{dep}=={get_wheel_version(dep, package_path)}"
                     for dep in WHEEL_DEPENDENCIES[_name]]
    if _name == "PySide6":
        _dependencies.append('tomli>=2.0.1; python_version < "3.11"')

    # Optional ('extras') dependencies, e.g. PySide6_Addons[webengine] or
    # PySide6[full]. Some are restricted to specific platforms; see
    # WHEEL_OPTIONAL_DEPENDENCIES.
    _optional_deps_block = ""
    _extras = {
        extra_name: deps
        for extra_name, (deps, platforms) in WHEEL_OPTIONAL_DEPENDENCIES.get(_name, {}).items()
        if platforms is None or sys.platform in platforms
    }
    if _extras:
        _lines = ["[project.optional-dependencies]"]
        for extra_name, deps in _extras.items():
            _pinned = [f'"{dep}=={get_wheel_version(dep, package_path)}"' for dep in deps]
            _lines.append(f"{extra_name} = [{', '.join(_pinned)}]")
        _optional_deps_block = "\n".join(_lines)

    minor_versions = python_minor_version_range(build_type)
    requires_python = f">=3.{minor_versions[0]}, <3.{minor_versions[1] + 1}"
    limited_api = f"cp3{minor_versions[0]}"

    with open(artifacts / "pyproject.toml.base", encoding="utf-8") as f:
        content = (
            f.read()
            .replace('"PROJECT_NAME"', f'"{setup.name}"')
            .replace('"PROJECT_VERSION"', f'"{setup.version}"')
            .replace('"PROJECT_DESCRIPTION"', f'"{setup.description}"')
            .replace('"PROJECT_README"', f'"{setup.readme}"')
            .replace('"PROJECT_TAG"', f'"{_tag}"')
            .replace('# PROJECT_SCRIPTS', _console_scripts)
            .replace('"PROJECT_DEPENDENCIES"', f"{_dependencies}")
            .replace('# PROJECT_OPTIONAL_DEPENDENCIES', _optional_deps_block)
            .replace('"PYTHON_LANGUAGE_VERSIONS",', python_language_versions(minor_versions))
            .replace("REQUIRES_PYTHON", requires_python)
            .replace('PY_LIMITED_API', limited_api)
            .replace('"PROJECT_PACKAGES"', json.dumps(packages))
        )
    return content


def get_manifest_files(package_path: Path, manifest_content: str) -> list[Path]:
    """Return the files that 'manifest_content' (a 'MANIFEST.in') selects in
    'package_path', evaluated the same way setuptools does when building the
    wheel."""
    file_list = FileList()
    cwd = os.getcwd()

    # Avoid duplicate warnings during process_template_line calls when no file matches a template
    # line. These will be shown later implicitly by the build itself.
    logging.disable(logging.WARNING)

    try:
        os.chdir(package_path)
        for line in manifest_content.splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                file_list.process_template_line(line)
    finally:
        os.chdir(cwd)
        logging.disable(logging.NOTSET)

    return [package_path / f for f in file_list.files]


def get_packages(package_path: Path, manifest_content: str) -> list[str]:
    """Return a list of packages for a wheel, with the exact directories it should contain based
    on processing the 'MANIFEST.in'.

    The different wheels (Essentials, Addons) are built from the same staging directory,
    so implicit package discovery would cause each wheel to contain the Python modules of all the
    others. Specifying the exact list of packages to include avoids this problem.

    Directories with data files are listed as well, so that setuptools
    doesn't warn about them being absent from the 'packages' configuration.

    Files in directories that can't be packages (e.g. 'Designer.app') are data
    of the closest enclosing package."""
    packages = set()
    for file in get_manifest_files(package_path, manifest_content):
        # e.g. ('PySide6', 'Qt', 'lib') for 'PySide6/Qt/lib/libfoo.dylib'.
        parts = file.parent.relative_to(package_path).parts
        # Bytecode caches are not packages.
        if "__pycache__" in parts:
            continue

        # Use the parts up to the first non-identifier, e.g. 'PySide6/Designer.app' -> 'PySide6'.
        package_parts = []
        for part in parts:
            if not part.isidentifier():
                break
            package_parts.append(part)
        # Files at the top level of package_path belong to no package.
        if package_parts:
            packages.add(".".join(package_parts))
    return sorted(packages)


# Maps a wheel name to the list of SBOM directory names to be included for that
# wheel. The 'PySide6' meta and examples wheels currently do not bundle an
# SBOM.
WHEEL_SBOM_DIRS = {
    "PySide6_Essentials": ["PySide6-Essentials", "PySide6-Tools-Essentials"],
    "PySide6_Addons": ["PySide6-Addons", "PySide6-Tools-Addons"],
    "PySide6_WebEngine": ["PySide6-WebEngine"],
    "PySide6_Pdf": ["PySide6-Pdf"],
    "shiboken6": ["shiboken6"],
    "shiboken6_generator": ["shiboken6_generator"],
}


def generate_setup_py(artifacts: Path, setup: SetupData, sbom_root: Path | None,
                      exclude_package_data: dict[str, list[str]] | None = None):
    _name = setup.name

    # To get the 'abi3' tag on the wheel name, we need to use
    # a fake extension to activate the limited-api option.
    # Because of the order of copying, we will use a name of a real
    # module for each package, so it gets overwrited.
    if _name == "PySide6":
        fext = "PySide6/QtCore"
    elif _name == "PySide6_Addons":
        fext = "PySide6/Qt3DCore"
    elif _name == "PySide6_WebEngine":
        fext = "PySide6/WebEngineCore"
    elif _name == "PySide6_Pdf":
        fext = "PySide6/QtPdf"
    else:
        fext = "Shiboken"

    # Compute the SBOM dir names this wheel will include from the CMake install
    # prefix. Empty when the wheel includes no SBOM.
    sbom_dirs = WHEEL_SBOM_DIRS.get(setup.name, []) if sbom_root else []

    # Skip SBOM info for Addon Qt tools when --no-qt-tools is passed, aka the directory
    # does not exist.
    tools_addons_dir = "PySide6-Tools-Addons"
    if tools_addons_dir in sbom_dirs and not (sbom_root / tools_addons_dir).is_dir():
        sbom_dirs = [d for d in sbom_dirs if d != tools_addons_dir]

    sbom_dirs = [(sbom_root / sbom_dir).resolve().as_posix() for sbom_dir in sbom_dirs]

    # For special wheels based on 'PySide6'
    # we force the name to be PySide6 for the package_name,
    # so we can take the files from that packaged-directory
    if setup.name in ("PySide6_Essentials", "PySide6_Addons",
                      "PySide6_Examples", "PySide6_WebEngine", "PySide6_Pdf"):
        _name = "PySide6"

    # Pass the repo root so the generated setup.py can import build_scripts.
    repo_root = Path(__file__).resolve().parent

    with open(artifacts / "setup.py.base", encoding="utf-8") as f:
        content = f.read().format(
            name=_name,
            fake_ext=fext,
            repo_root=repo_root.as_posix(),
            sbom_dirs=repr(sbom_dirs),
            exclude_package_data=exclude_package_data or {},
        )

    return content


def get_unwanted_pyi_files(data: list[ModuleData] | None, package_path: Path) -> list[str]:
    """Return the '.pyi' stub file names present in the 'PySide6' package
    directory that don't belong to the given wheel's modules.

    setuptools implicitly globs and bundles *every* '*.pyi' file it finds in
    a package directory (see
    'setuptools.command.build_py._IMPLICIT_DATA_FILES'), no matter what
    'MANIFEST.in'/'package_data' declare. Because 'PySide6_Essentials',
    'PySide6_Addons' and 'PySide6_WebEngine' are all built from the same
    'PySide6' directory, every wheel would otherwise end up bundling the
    '.pyi' stubs that belong to modules packaged in a *different* wheel.
    """
    if data is None:
        return []

    wanted = set()
    for module in data:
        wanted.update(module.pyi)
        wanted.update(f for f in module.extra_files if f.endswith(".pyi"))

    all_pyi = {f.name for f in (package_path / "PySide6").glob("*.pyi")}
    return sorted(all_pyi - wanted)


def wheel_shiboken_generator(package_path: Path) -> tuple[SetupData, None]:
    setup = SetupData(
        name="shiboken6_generator",
        version=get_wheel_version("shiboken6_generator", package_path),
        description="Python/C++ bindings generator",
        readme="README.shiboken6-generator.md",
        console_scripts=[
            'shiboken6 = "shiboken6_generator.scripts.shiboken_tool:main"',
            'shiboken6-genpyi = "shiboken6_generator.scripts.shiboken_tool:genpyi"',
        ],
    )

    return setup, None


def wheel_shiboken_module(package_path: Path) -> tuple[SetupData, None]:
    setup = SetupData(
        name="shiboken6",
        version=get_wheel_version("shiboken6", package_path),
        description="Python/C++ bindings helper module",
        readme="README.shiboken6.md",
        console_scripts=[],
    )

    return setup, None


def wheel_pyside6_essentials(package_path: Path) -> tuple[SetupData, list[ModuleData]]:
    packaged_qt_tools_path = package_path / "PySide6"
    set_pyside_package_path(packaged_qt_tools_path)
    _pyside_tools = available_pyside_tools(packaged_qt_tools_path, package_for_wheels=True)

    # replacing pyside6-android_deploy by pyside6-android-deploy for consistency
    # Also, the tool should not exist in any other platform than Linux and macOS
    _console_scripts = []
    if "android_deploy" in _pyside_tools:
        if sys.platform in ("linux", "darwin"):
            _console_scripts = ['pyside6-android-deploy = '
                                '"PySide6.scripts.pyside_tool:android_deploy"']
        _pyside_tools.remove("android_deploy")

    # replacing pyside6-ios_deploy by pyside6-ios-deploy for consistency
    # Also, the tool should not exist in any other platform than macOS
    if "ios_deploy" in _pyside_tools:
        if ("ios_deploy" in _pyside_tools) and sys.platform == "darwin":
            _console_scripts.append('pyside6-ios-deploy = '
                                    '"PySide6.scripts.pyside_tool:ios_deploy"')
        _pyside_tools.remove("ios_deploy")

    _console_scripts.extend([f'pyside6-{tool} = "PySide6.scripts.pyside_tool:{tool}"'
                            for tool in _pyside_tools])

    setup = SetupData(
        name="PySide6_Essentials",
        version=get_wheel_version("PySide6_Essentials", package_path),
        description=f"{PYSIDE_DESCRIPTION} (Essentials)",
        readme="README.pyside6_essentials.md",
        console_scripts=_console_scripts
    )

    data = wheel_files_pyside_essentials()

    return setup, data


def wheel_pyside6_addons(package_path: Path) -> tuple[SetupData, list[ModuleData]]:
    setup = SetupData(
        name="PySide6_Addons",
        version=get_wheel_version("PySide6_Addons", package_path),
        description=f"{PYSIDE_DESCRIPTION} (Addons)",
        readme="README.pyside6_addons.md",
        console_scripts=[],
    )

    data = wheel_files_pyside_addons()

    return setup, data


def wheel_pyside6_webengine(package_path: Path) -> tuple[SetupData, list[ModuleData]]:
    setup = SetupData(
        name="PySide6_WebEngine",
        version=get_wheel_version("PySide6_WebEngine", package_path),
        description=f"{PYSIDE_DESCRIPTION} (WebEngine)",
        readme="README.pyside6_webengine.md",
        console_scripts=[],
    )

    data = wheel_files_pyside_webengine()

    return setup, data


def wheel_pyside6_pdf(package_path: Path) -> tuple[SetupData, list[ModuleData]]:
    setup = SetupData(
        name="PySide6_Pdf",
        version=get_wheel_version("PySide6_Pdf", package_path),
        description=f"{PYSIDE_DESCRIPTION} (Pdf)",
        readme="README.pyside6_pdf.md",
        console_scripts=[],
    )

    data = wheel_files_pyside_pdf()

    return setup, data


def wheel_pyside6(package_path: Path) -> tuple[SetupData, list[ModuleData] | None]:
    setup = SetupData(
        name="PySide6",
        version=get_wheel_version("PySide6", package_path),
        description=PYSIDE_DESCRIPTION,
        readme="README.pyside6.md",
        console_scripts=[],
    )

    return setup, None


def wheel_pyside6_examples(package_path: Path) -> tuple[SetupData, list[ModuleData] | None]:
    setup = SetupData(
        name="PySide6_Examples",
        version=get_wheel_version("PySide6_Examples", package_path),
        description="Examples for the Qt for Python project",
        readme="README.pyside6_examples.md",
        console_scripts=[],
    )

    return setup, None


def copy_examples_for_wheel(package_path: Path):
    # Copying examples
    try:
        copytree("examples", package_path / "PySide6" / "examples", dirs_exist_ok=True)
    except OSError as e:
        log.error(f"Error trying to copy the examples directory: {e}")
        sys.exit(-1)


def venv_name():
    v = os.environ.get("VIRTUAL_ENV")
    return Path(v).name if v else None


def get_build_directory(options: Namespace) -> tuple[Path, BuildType]:
    build_dir = Path("build")
    # Search for a "--env" option first", try postfix "a" for limited API or "d", debug
    venv = options.env if options.env else venv_name()
    if venv:
        for build_type, postfix in BUILDTYPE_POSTFIX_MAPPING:
            result = build_dir / f"{venv}{postfix}"
            if result.is_dir():
                return result, build_type
    if options.env:
        raise ValueError(f'Invalid environment "{options.env}" passed')
    # Try explicit build-dir
    if options.build_dir and (Path(options.build_dir) / PACKAGE_FOR_WHEELS).is_dir():
        return Path(options.build_dir), BuildType.LIMITED_API
    # Fallback to existing dirs (skip "config.tests")
    for d in build_dir.glob("*"):
        if (d / PACKAGE_FOR_WHEELS).is_dir():
            log.warning(
                "No valid environment or build directory was specified, so create_wheels is using "
                "the first valid directory it could find on its own. If this is not the one you "
                "want, use the --env or --build-dir options to provide it explicitly."
            )
            return d, BuildType.LIMITED_API
    raise FileNotFoundError(
        "Unable to determine build directory, no matching virtual environment found")


def module_names_from_wheel_function(func) -> set[str]:
    """Return the Qt module short names referenced by a wheel_files_pyside_* function.

    The names are extracted from the function's source (the 'module_Qt<Name>()' calls) rather
    than by calling it, because calling it requires the package paths to be set up first.
    """
    source = inspect.getsource(func)
    return set(re.findall(r"module_Qt([A-Za-z0-9]+)\(\)", source))


def module_names_from_cmake_list(cmake_text: str, list_name: str) -> set[str]:
    """Return the module names defined in a `set(<list_name> ...)` block of a CMake file."""
    match = re.search(rf"set\(\s*{list_name}\b(.*?)\)", cmake_text,
                      re.DOTALL)
    if not match:
        return set()
    return set(match.group(1).split())


def python_wheel_modules() -> dict[str, set[str]]:
    """Return the module short names of each wheel_files_pyside_<wheel>() function, by wheel."""
    prefix = "wheel_files_pyside_"
    wheels = {}
    for func_name in dir(build_scripts.wheel_files):
        if not func_name.startswith(prefix):
            continue
        wheel = func_name.removeprefix(prefix)
        func = getattr(build_scripts.wheel_files, func_name)
        wheels[wheel] = module_names_from_wheel_function(func)
    return wheels


def cmake_wheel_modules(cmake_text: str) -> dict[str, set[str]]:
    """Return the module names of each PYSIDE_<WHEEL>_WHEEL_MODULES list, by lower-case wheel."""
    pattern = r"set\(\s*PYSIDE_(\w+)_WHEEL_MODULES\b(.*?)\)"
    wheels = {}
    for wheel, modules in re.findall(pattern, cmake_text, re.DOTALL):
        if wheel == "QT_TOOLS":
            continue
        wheels[wheel.lower()] = set(modules.split())
    return wheels


def check_wheel_module_lists_consistency() -> list[str]:
    """Return the differences between the per-wheel module lists of Python and CMake.

    build_scripts/wheel_files.py defines the wheel contents, while the
    PYSIDE_<WHEEL>_WHEEL_MODULES lists in `mapping_cmake` control how the
    built modules are split across the per-wheel SBOM documents. The two must
    stay in sync, otherwise the SBOM documents stop matching the wheels.

    Each wheel_files_pyside_<wheel>() function also needs a WHEEL_SBOM_DIRS entry, and each
    wheel other than Essentials and Addons needs to be in PYSIDE_SEPARATE_SBOM_WHEELS, so that
    the wheel gets its own SBOM document.
    """
    mapping_cmake = Path("sources/pyside6/cmake/PySideSbomWheelMapping.cmake")
    if not mapping_cmake.is_file():
        return [f"Could not find '{mapping_cmake}' to check the per-wheel module lists."]

    cmake_text = mapping_cmake.read_text(encoding="utf-8")
    python_wheels = python_wheel_modules()
    cmake_wheels = cmake_wheel_modules(cmake_text)
    separate_sbom_wheels = {wheel.lower() for wheel in module_names_from_cmake_list(
        cmake_text, "PYSIDE_SEPARATE_SBOM_WHEELS")}
    sbom_dir_wheels = {wheel.lower().removeprefix("pyside6_") for wheel in WHEEL_SBOM_DIRS}
    problems = []

    for wheel in sorted(cmake_wheels.keys() - python_wheels.keys()):
        problems.append(f"PYSIDE_{wheel.upper()}_WHEEL_MODULES in {mapping_cmake} has no "
                        f"matching wheel_files_pyside_{wheel}() function in "
                        "build_scripts/wheel_files.py.")

    for wheel, python_modules in sorted(python_wheels.items()):
        cmake_list = f"PYSIDE_{wheel.upper()}_WHEEL_MODULES"
        if wheel not in cmake_wheels:
            problems.append(f"The PySide6 {wheel} wheel has no {cmake_list} list in "
                            f"{mapping_cmake}.")
            continue

        if wheel not in ("essentials", "addons") and wheel not in separate_sbom_wheels:
            problems.append(f"The PySide6 {wheel} wheel is missing from "
                            f"PYSIDE_SEPARATE_SBOM_WHEELS in {mapping_cmake}.")

        if wheel not in sbom_dir_wheels:
            problems.append(f"The PySide6 {wheel} wheel has no entry in WHEEL_SBOM_DIRS "
                            "in create_wheels.py.")

        only_in_python = python_modules - cmake_wheels[wheel]
        only_in_cmake = cmake_wheels[wheel] - python_modules
        if only_in_python or only_in_cmake:
            problem = (f"The PySide6 {wheel} module list is out of sync between Python "
                       f"(wheel_files_pyside_{wheel} in build_scripts/wheel_files.py) and "
                       f"CMake ({cmake_list} in {mapping_cmake}):")
            if only_in_python:
                problem += f"\n  only in Python: {sorted(only_in_python)}"
            if only_in_cmake:
                problem += f"\n  only in CMake:  {sorted(only_in_cmake)}"
            problems.append(problem)

    return problems


def check_qt_tool_wheel_modules_consistency() -> list[str]:
    """Return the Qt tools that are assigned to a different module in CMake than in Python,
    or that are not assigned to any module in CMake.

    The module_Qt<module>() functions in build_scripts/wheel_files.py decide which wheel ships a
    Qt tool, while PYSIDE_QT_TOOLS_WHEEL_MODULES decides which per-wheel pyside-tools SBOM
    document lists it. Needs set_pyside_package_path() to have been called.
    """
    mapping_cmake = Path("sources/pyside6/cmake/PySideSbomWheelMapping.cmake")
    # Already reported by check_wheel_module_lists_consistency().
    if not mapping_cmake.is_file():
        return []

    cmake_text = mapping_cmake.read_text(encoding="utf-8")
    problems = []
    entries = module_names_from_cmake_list(cmake_text, "PYSIDE_QT_TOOLS_WHEEL_MODULES")
    module_patterns = {}
    for entry in sorted(entries):
        tool, _, module = entry.partition(":")
        if module not in module_patterns:
            module_func = getattr(build_scripts.wheel_files, f"module_Qt{module}", None)
            module_patterns[module] = None
            if module_func:
                data = module_func()
                module_patterns[module] = [Path(p).name
                                           for p in data.extra_files + data.extra_dirs]
        patterns = module_patterns[module]
        assignment = (f"The Qt tool '{tool}' is assigned to Qt{module} in "
                      f"PYSIDE_QT_TOOLS_WHEEL_MODULES in {mapping_cmake}")
        if patterns is None:
            problems.append(f"{assignment}, but build_scripts/wheel_files.py has no "
                            f"module_Qt{module}() function.")
            continue
        names = (tool, f"{tool}.exe", f"{tool.capitalize()}.app")
        if not any(fnmatch(name, pattern) for name in names for pattern in patterns):
            problems.append(f"{assignment}, but module_Qt{module}() in "
                            "build_scripts/wheel_files.py doesn't ship it.")

    # PYSIDE_WINDOWS_BIN_TOOLS lists all the Qt tools, on all platforms.
    mapped_tools = {entry.partition(":")[0] for entry in entries}
    for tool in sorted(set(PYSIDE_WINDOWS_BIN_TOOLS) - mapped_tools):
        problems.append(f"The Qt tool '{tool}' is shipped (see build_scripts/__init__.py), but "
                        f"is missing from PYSIDE_QT_TOOLS_WHEEL_MODULES in {mapping_cmake}.")

    return problems


def check_sbom_wheel_mapping(package_path: Path, warn_only: bool):
    """Fail if the CMake SBOM wheel mapping doesn't match build_scripts/wheel_files.py.

    All problems are reported before exiting. With 'warn_only', they are reported as warnings
    and the wheel creation continues.
    """
    # The module_Qt<module>() functions used by the Qt tools check read the module JSON files.
    set_pyside_package_path(package_path / "PySide6")
    problems = check_wheel_module_lists_consistency() + check_qt_tool_wheel_modules_consistency()
    if not problems:
        return

    report = log.warning if warn_only else log.error
    for problem in problems:
        report(problem)

    if not warn_only:
        log.error("The SBOM wheel mapping is inconsistent. Fix the lists above, or pass "
                  "--sbom-mapping-warn-only to continue anyway.")
        sys.exit(1)


def check_modules_consistency():
    available_functions = dir(build_scripts.wheel_files)
    functions = [i.replace("module_", "") for i in available_functions if i.startswith("module_")]

    sources = [i.stem for i in Path("sources/pyside6/PySide6/").glob("Qt*")]

    missing_modules = set(sources) - set(functions)

    if missing_modules:
        log.warning("The following modules don't have a function "
                    f"in 'build_scripts/wheel_files.py':\n  {missing_modules}")

    # Check READMEs
    readme_modules = set()
    for r in Path(".").glob("README.pyside6*"):
        with open(r, encoding="utf-8") as f:
            for line in f:
                if line.startswith("* Qt"):
                    readme_modules.add(line.strip().replace("* ", ""))

    missing_modules_readme = set(sources) - readme_modules

    if missing_modules_readme:
        log.warning(f"The following modules are not in READMEs:\n  {missing_modules_readme}")


if __name__ == "__main__":

    parser = ArgumentParser()
    parser.add_argument('--verbose', '-v', type=int, help='Verbose level')
    # Command line option to find the build/<envname>a/package_for_wheels
    parser.add_argument(
        "--env", type=str, default=None,
        help="The env's name from which PySide was built such that the "
             "build directory is 'build/<envname>' (must contain a "
             "'package_for_wheels' folder"
    )
    # Alternatively, <build-dir> (must contain "package_for_wheels")
    parser.add_argument(
        "--build-dir", type=str, default=None,
        help="The directory where PySide was build (must contain a "
             "'package_for_wheels' folder"
    )
    parser.add_argument("--no-examples", "-e", action="store_true",
                        help="Do not build examples wheel")
    parser.add_argument("--sbom", action="store_true",
                        help="Include SBOM documents in the wheels")
    parser.add_argument("--sbom-mapping-warn-only", action="store_true",
                        help="Report mismatches between PySideSbomWheelMapping.cmake and "
                             "build_scripts/wheel_files.py as warnings instead of failing")
    options = parser.parse_args()

    # Sanity check between the available modules,
    # and the functions in build_scripts/wheel_files.py
    check_modules_consistency()

    build_directory, build_type = get_build_directory(options)

    verbose = options.verbose if options.verbose else 0
    # Setup paths
    current_path = Path(__file__).resolve().parent
    artifacts_path = Path("wheel_artifacts/")
    # the extra 'a' is for compatibility with the build_scripts
    # notation that adds an 'a' when using limited-api
    package_path = build_directory / PACKAGE_FOR_WHEELS
    log.info(f'Using build dir "{build_directory.name}"')

    # Check for 'package_for_wheels' directory
    if not package_path.is_dir():
        log.error(f"Couldn't find the directory: {package_path}")
        log.error("Maybe your build used '--skip-packaging'? Exiting.")
        sys.exit(-1)

    setup_py_path = package_path / "setup.py"
    pyproject_toml_path = package_path / "pyproject.toml"

    base_files = [
        current_path / "LICENSES/GFDL-1.3-no-invariants-only.txt",
        current_path / "LICENSES/LicenseRef-Qt-Commercial.txt",
        current_path / "LICENSES/GPL-2.0-only.txt",
        current_path / "LICENSES/GPL-3.0-only.txt",
        current_path / "LICENSES/Qt-GPL-exception-1.0.txt",
        current_path / "LICENSES/LGPL-3.0-only.txt",
    ]

    # Main generation
    wheels = {
        "shiboken6": wheel_shiboken_module,
        "shiboken6_generator": wheel_shiboken_generator,
        "PySide6_Essentials": wheel_pyside6_essentials,
        "PySide6_Addons": wheel_pyside6_addons,
        "PySide6_WebEngine": wheel_pyside6_webengine,
        "PySide6_Pdf": wheel_pyside6_pdf,
        "PySide6": wheel_pyside6,
    }
    if not options.no_examples:
        wheels["PySide6_Examples"] = wheel_pyside6_examples

    # SBOM documents are installed into the CMake install prefix at
    # '<build_dir>/install/share/sbom/<wheel_name>'. The generated setup.py
    # needs this location to include the correct SBOM files in the wheel.
    sbom_install_root = build_directory / "install" / "share" / "sbom"
    if not options.sbom:
        sbom_install_root = None

    check_sbom_wheel_mapping(package_path, options.sbom_mapping_warn_only)

    for name, wheel_info in wheels.items():

        log.info(f"Starting process for: {name}")
        setup, data = wheel_info(package_path)

        # 1. Generate 'setup.py'
        log.info("-- Generating setup.py")
        exclude_package_data = None
        if data is not None:
            unwanted_pyi = get_unwanted_pyi_files(data, package_path)
            if unwanted_pyi:
                exclude_package_data = {"PySide6": unwanted_pyi}
        setup_py_content = generate_setup_py(artifacts_path, setup, sbom_install_root,
                                             exclude_package_data)
        with open(setup_py_path, "w", encoding="utf-8") as f:
            f.write(setup_py_content)

        # 2. Create PySide_Essentials.json and PySide_Addons.json
        # creates a json file mapping each Qt module to the possible plugin dependencies
        if data is not None:
            log.info(f"-- Creating {name}.json")
            create_module_plugin_json(name, data, package_path)

        # 3. Create the 'MANIFEST.in'
        # Special case for shiboken and shiboken_generator
        # so we copy the whole directory, only PySide and derivatives
        # will need to have specific information
        log.info("-- Creating MANIFEST.in")
        if data is None:
            manifest_content = get_simple_manifest(name)
        else:
            manifest_content = get_manifest(name, data, package_path, verbose)
        # Bytecode from the build is interpreter specific, so exclude it. Also avoids warnings.
        manifest_content += "\nglobal-exclude *.pyc\n"
        with open(package_path / "MANIFEST.in", "w", encoding="utf-8") as f:
            f.write(manifest_content)

        # 4. copy configuration files to create the wheel
        log.info("-- Copy configuration files to create the wheel")
        if name == "PySide6_Examples":
            copy_examples_for_wheel(package_path)
        _files: list[Path] = base_files + [Path(setup.readme)]
        for fname in _files:
            copy(fname, package_path)

        # 5. Generate 'pyproject.toml'. The packages depend on 'MANIFEST.in' and the
        # copied examples.
        log.info("-- Generating pyproject.toml")
        pyproject_toml_content = generate_pyproject_toml(artifacts_path, setup, package_path,
                                                         build_type,
                                                         get_packages(package_path,
                                                                      manifest_content))
        with open(pyproject_toml_path, "w", encoding="utf-8") as f:
            f.write(pyproject_toml_content)

        # 6. call the build module to create the wheel
        log.info("-- Creating wheels")
        if verbose < 2:
            _runner = pyproject_hooks.quiet_subprocess_runner
        else:
            _runner = pyproject_hooks.default_subprocess_runner
        builder = build.ProjectBuilder(package_path, runner=_runner)
        builder.build("wheel", "dist")

        # 7. Copy wheels back
        log.info("-- Copying wheels to dist/")
        dist_path = Path("dist")
        if not dist_path.is_dir():
            dist_path.mkdir()
        for wheel in (package_path / "dist").glob("*.whl"):
            copy(wheel, dist_path / wheel.name)

        # 8. Remove leftover files
        log.info("-- Removing leftover files")
        all_files = set(package_path.glob("*"))
        files_to_remove = all_files - {
            package_path / i for i in ("PySide6", "shiboken6", "shiboken6_generator")
        }
        for _f in files_to_remove:
            if _f.is_dir():
                rmtree(_f)
            elif _f.is_file():
                _f.unlink()
