# Copyright (C) 2018 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import functools
import importlib
import importlib.util
import json
import os
import platform
import re
import sys
import subprocess
import sysconfig
import time
from packaging.version import parse as parse_version
from pathlib import Path
from shutil import copytree, rmtree

# PYSIDE-1760: Pre-load setuptools modules early to avoid racing conditions.
#              may be touched (should be avoided anyway, btw.)
# Note: This bug is only visible when tools like pyenv are not used. They have some
#       pre-loading effect so that setuptools is already in the cache, hiding the problem.
from setuptools import Command, Extension
from setuptools.command.bdist_egg import bdist_egg as _bdist_egg
from setuptools.command.build_ext import build_ext as _build_ext
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.build import build as _build
from setuptools.command.develop import develop as _develop
from setuptools.command.install import install as _install
from setuptools.command.install_lib import install_lib as _install_lib
from setuptools.command.install_scripts import install_scripts  # noqa: preload only

from .log import log, LogLevel
from setuptools.errors import SetupError

from .build_info_collector import BuildInfoCollectorMixin
from .config import config
from .options import OPTION, CommandMixin
from .platforms.unix import prepare_packages_posix
from .platforms.windows_desktop import prepare_packages_win32
from .qtinfo import QtInfo
from .utils import (copydir, copyfile, detect_clang,
                    get_numpy_location, get_python_dict,
                    linux_fix_rpaths_for_library, macos_fix_rpaths_for_library, parse_modules,
                    platform_cmake_options, remove_tree, run_process,
                    run_process_output, RpathTargetType, update_env_path, which)
from . import PYSIDE, PYSIDE_MODULE, SHIBOKEN, SHIBOKEN_GENERATOR
from .wheel_override import get_bdist_wheel_override, wheel_module_exists
from .wheel_utils import (get_package_timestamp, get_package_version,
                          macos_plat_name, macos_pyside_min_deployment_target)

setup_script_dir = Path.cwd()
build_scripts_dir = setup_script_dir / 'build_scripts'
setup_py_path = setup_script_dir / "setup.py"

start_time = time.time()


def elapsed():
    return int(time.time() - start_time)


def get_setuptools_extension_modules():
    # Setting py_limited_api on the extension is the "correct" thing
    # to do, but it doesn't actually do anything, because we
    # override build_ext. So this is just foolproofing for the
    # future.
    extension_args = ('QtCore', [])
    extension_kwargs = {}
    if OPTION["LIMITED_API"] == 'yes':
        extension_kwargs['py_limited_api'] = True
    extension_modules = [Extension(*extension_args, **extension_kwargs)]
    return extension_modules


def _get_make(platform_arch, build_type):
    """Helper for retrieving the make command and CMake generator name"""
    makespec = OPTION["MAKESPEC"]
    if makespec == "make":
        return ("make", "Unix Makefiles")
    if makespec == "msvc":
        if not OPTION["NO_JOM"]:
            jom_path = Path(which("jom"))
            if jom_path:
                log.info(f"jom was found in {jom_path}")
                return (jom_path, "NMake Makefiles JOM")
        nmake_path = Path(which("nmake"))
        if nmake_path is None or not nmake_path.exists():
            raise SetupError("nmake not found")
        log.info(f"nmake was found in {nmake_path}")
        if OPTION["JOBS"]:
            msg = "Option --jobs can only be used with 'jom' on Windows."
            raise SetupError(msg)
        return (nmake_path, "NMake Makefiles")
    if makespec == "mingw":
        return (Path("mingw32-make"), "mingw32-make")
    if makespec == "ninja":
        return (Path("ninja"), "Ninja")
    raise SetupError(f'Invalid option --make-spec "{makespec}".')


def get_make(platform_arch, build_type):
    """Retrieve the make command and CMake generator name"""
    (make_path, make_generator) = _get_make(platform_arch, build_type)
    if not make_path.is_absolute():
        found_path = Path(which(make_path))
        if not found_path or not found_path.exists():
            m = (f"You need the program '{make_path}' on your system path to "
                 f"compile {PYSIDE_MODULE}.")
            raise SetupError(m)
        make_path = found_path
    return (make_path, make_generator)


@functools.cache
def get_allowed_python_versions():
    config_file = Path(__file__).parents[1] / "wheel_artifacts" / "python_versions.json"
    with config_file.open(encoding="utf-8") as json_file:
        json_data = json.load(json_file)
    min_minor_version = json_data["minimum_minor_version"]
    max_minor_version = json_data["maximum_minor_version"]
    return [(3, minor) for minor in range(min_minor_version, max_minor_version + 1)]


def check_allowed_python_version():
    """
    Make sure that setup.py is run with an allowed python version.
    """

    supported = get_allowed_python_versions()
    this_py = sys.version_info[:2]
    if this_py not in supported:
        log.warning("*" * 80)
        log.warning(f"Unsupported Python version detected: {this_py}.")
        log.warning("The build will probably fail.")
        log.warning(f"Supported versions: {supported}")
        log.warning("*" * 80)


def prepare_build():
    # locate Qt sources for the documentation
    if OPTION["QT_SRC"]:
        return OPTION["QT_SRC"]
    install_prefix = QtInfo().prefix_dir
    if install_prefix:
        # In-source, developer build
        if install_prefix.endswith("qtbase"):
            return install_prefix
        else:  # SDK: Use 'Src' directory
            maybe_qt_src_dir = Path(install_prefix).parent / 'Src' / 'qtbase'
            if maybe_qt_src_dir.exists():
                return maybe_qt_src_dir
    return ''


def get_soname(clang_lib_path: Path) -> str | None:
    """Getting SONAME from a shared library using readelf. Works only on Linux.
    """
    clang_lib_path = Path(clang_lib_path)
    try:
        result = subprocess.run(['readelf', '-d', str(clang_lib_path)],
                                capture_output=True, text=True, check=True)
        for line in result.stdout.split('\n'):
            if 'SONAME' in line:
                soname = line.split('[')[1].split(']')[0]
                return soname
    except subprocess.CalledProcessError as e:
        print(f"Failed to get SONAME: {e}")
    return None


class PysideInstall(_install, CommandMixin):

    user_options = _install.user_options + CommandMixin.mixin_user_options

    def __init__(self, *args, **kwargs):
        self.command_name = "install"
        _install.__init__(self, *args, **kwargs)
        CommandMixin.__init__(self)

    def initialize_options(self):
        _install.initialize_options(self)

    def finalize_options(self):
        CommandMixin.mixin_finalize_options(self)
        _install.finalize_options(self)

        if sys.platform == 'darwin' or self.is_cross_compile:
            # Because we change the plat_name to include a correct
            # deployment target on macOS setuptools thinks we are
            # cross-compiling, and throws an exception when trying to
            # execute setup.py install. The check looks like this
            # if self.warn_dir and build_plat != get_platform():
            #   raise PlatformError("Can't install when "
            #                       "cross-compiling")
            # Obviously get_platform will return the old deployment
            # target. The fix is to disable the warn_dir flag, which
            # was created for bdist_* derived classes to override, for
            # similar cases.
            # We also do it when cross-compiling. While calling install
            # command directly is dubious, bdist_wheel calls install
            # internally before creating a wheel.
            self.warn_dir = False

    def run(self):
        _install.run(self)
        log.info(f"--- Install completed ({elapsed()}s)")


class PysideDevelop(_develop):

    def __init__(self, *args, **kwargs):
        _develop.__init__(self, *args, **kwargs)

    def run(self):
        self.run_command("build")
        _develop.run(self)


class PysideBdistEgg(_bdist_egg):

    def __init__(self, *args, **kwargs):
        _bdist_egg.__init__(self, *args, **kwargs)

    def run(self):
        self.run_command("build")
        _bdist_egg.run(self)


class PysideBuildExt(_build_ext):

    def __init__(self, *args, **kwargs):
        _build_ext.__init__(self, *args, **kwargs)

    def run(self):
        pass


class PysideBuildPy(_build_py):

    def __init__(self, *args, **kwargs):
        self.command_name = "build_py"
        _build_py.__init__(self, *args, **kwargs)


# _install_lib is reimplemented to preserve
# symlinks when setuptools copy files to various
# directories from the setup tools build dir to the install dir.
class PysideInstallLib(_install_lib):

    def __init__(self, *args, **kwargs):
        _install_lib.__init__(self, *args, **kwargs)

    def install(self):
        """
        Installs files from self.build_dir directory into final
        site-packages/PySide6 directory when the command is 'install'
        or into build/wheel when command is 'bdist_wheel'.
        """

        build_dir = Path(self.build_dir)
        if build_dir.is_dir():
            # Using our own copydir makes sure to preserve symlinks.
            outfiles = copydir(Path(build_dir).resolve(), Path(self.install_dir).resolve())
        else:
            self.warn(f"'{self.build_dir}' does not exist -- no Python modules to install")
            return
        return outfiles


class PysideBuild(_build, CommandMixin, BuildInfoCollectorMixin):

    user_options = _build.user_options + CommandMixin.mixin_user_options

    def __init__(self, *args, **kwargs):
        self.command_name = "build"
        _build.__init__(self, *args, **kwargs)
        CommandMixin.__init__(self)
        BuildInfoCollectorMixin.__init__(self)

    def finalize_options(self):
        os_name_backup = os.name
        CommandMixin.mixin_finalize_options(self)
        BuildInfoCollectorMixin.collect_and_assign(self)

        use_os_name_hack = False
        if self.is_cross_compile:
            use_os_name_hack = True
        elif sys.platform == 'darwin':
            self.plat_name = macos_plat_name()
            use_os_name_hack = True

        if use_os_name_hack:
            # This is a hack to circumvent the dubious check in
            # setuptool.commands.build -> finalize_options, which only
            # allows setting the plat_name for windows NT.
            # That is not the case for the wheel module though (which
            # does allow setting plat_name), so we circumvent by faking
            # the os name when finalizing the options, and then
            # restoring the original os name.
            os.name = "nt"

        _build.finalize_options(self)

        # Must come after _build.finalize_options
        BuildInfoCollectorMixin.post_collect_and_assign(self)

        if use_os_name_hack:
            os.name = os_name_backup

    def initialize_options(self):
        _build.initialize_options(self)
        self.make_path = None
        self.make_generator = None
        self.script_dir = None
        self.sources_dir = None
        self.build_dir = None
        self.install_dir = None
        self.py_executable = None
        self.py_include_dir = None
        self.py_library = None
        self.py_version = None
        self.py_arch = None
        self.build_type = "Release"
        self.qtinfo = None
        self.build_tests = False
        self.python_target_info = {}
        self.qt_src_dir = ''

    def run(self):
        self.qt_src_dir = prepare_build()

        # Check env
        make_path = None
        make_generator = None
        if not OPTION["ONLYPACKAGE"]:
            platform_arch = platform.architecture()[0]
            (make_path, make_generator) = get_make(platform_arch, self.build_type)

        self.qtinfo = QtInfo()
        # Update the PATH environment variable
        # Don't add Qt to PATH env var, we don't want it to interfere
        # with CMake's find_package calls which will use
        # CMAKE_PREFIX_PATH.
        # Don't add the Python scripts dir to PATH env when
        # cross-compiling, it could be in the device sysroot (/usr)
        # which can cause CMake device QtFooToolsConfig packages to be
        # picked up instead of host QtFooToolsConfig packages.
        additional_paths = []
        if self.py_scripts_dir and not self.is_cross_compile:
            additional_paths.append(self.py_scripts_dir)

        # Add Clang to path for Windows.
        # Revisit once Clang is bundled with Qt.
        if (sys.platform == "win32"
                and parse_version(self.qtinfo.version) >= parse_version("5.7.0")):
            clang_dir, clang_source = detect_clang()
            if clang_dir:
                clangBinDir = clang_dir / 'bin'
                if str(clangBinDir) not in os.environ.get('PATH'):
                    log.info(f"Adding {clangBinDir} as detected by {clang_source} to PATH")
                    additional_paths.append(clangBinDir)
            else:
                raise SetupError("Failed to detect Clang when checking "
                                 "LLVM_INSTALL_DIR, CLANG_INSTALL_DIR, llvm-config")

        update_env_path(additional_paths)

        self.make_path = make_path
        self.make_generator = make_generator

        self.build_tests = OPTION["BUILDTESTS"]

        # Save the shiboken generator build dir path for clang deployment
        # purposes.
        self.shiboken_generator_build_dir = self.build_dir / SHIBOKEN_GENERATOR

        self.log_pre_build_info()

        # Prepare folders
        if not self.sources_dir.exists():
            log.info(f"Creating sources folder {self.sources_dir}...")
            os.makedirs(self.sources_dir)
        if not self.build_dir.exists():
            log.info(f"Creating build folder {self.build_dir}...")
            os.makedirs(self.build_dir)
        if not self.install_dir.exists():
            log.info(f"Creating install folder {self.install_dir}...")
            os.makedirs(self.install_dir)

        # Write the CMake install path into a file. Is used by
        # SetupRunner to provide a nicer UX when cross-compiling (no
        # need to specify a host shiboken path explicitly)
        if self.internal_cmake_install_dir_query_file_path:
            with open(self.internal_cmake_install_dir_query_file_path, 'w') as f:
                f.write(os.fspath(self.install_dir))

        if not OPTION["ONLYPACKAGE"]:
            # Build extensions
            for ext in config.get_buildable_extensions():
                self.build_extension(ext)

            # We always record the history, whether tests are built or not.
            # Record the latest successful build and note the
            # build directory for supporting the tests or other tools.
            timestamp = time.strftime('%Y-%m-%d_%H%M%S')
            build_history = setup_script_dir / 'build_history'
            unique_dir = build_history / timestamp
            unique_dir.mkdir(parents=True)
            fpath = unique_dir / 'build_dir.txt'
            with open(fpath, 'w') as f:
                print(self.build_dir, file=f)
                print(self.build_classifiers, file=f)
            log.info(f"Created {build_history}")

        if not OPTION["SKIP_PACKAGING"]:
            # Build patchelf if needed
            self.build_patchelf()

            # Prepare packages
            self.prepare_packages()

            # Build packages
            _build.run(self)

            # Keep packaged directories for wheel construction
            # This is to take advantage of the packaging step
            # to keep the data in the proper structure to create
            # a wheel.
            _path = Path(self.st_build_dir)
            _wheel_path = _path.parent / "package_for_wheels"

            _project = None

            if config.is_internal_shiboken_module_build():
                _project = "shiboken6"
            elif config.is_internal_shiboken_generator_build():
                _project = "shiboken6_generator"
            elif config.is_internal_pyside_build():
                _project = "PySide6"

            if _project is not None:
                if not _wheel_path.exists():
                    _wheel_path.mkdir(parents=True)
                _src = Path(_path / _project)
                _dst = Path(_wheel_path / _project)
                # Remove the directory in case it exists.
                # This applies to 'shiboken6', 'shiboken6_generator',
                # and 'pyside6' inside the 'package_for_wheels' directory.
                if _dst.exists():
                    log.warning(f'Found directory "{_dst}", removing it first.')
                    remove_tree(_dst)

                try:
                    # This should be copied because the package directory
                    # is used when using the 'install' setup.py instruction.
                    copytree(_src, _dst)
                except Exception as e:
                    log.warning(f'problem renaming "{self.st_build_dir}"')
                    log.warning(f'ignored error: {type(e).__name__}: {e}')

        else:
            log.info("Skipped preparing and building packages.")
        log.info(f"--- Build completed ({elapsed()}s)")

    def log_pre_build_info(self):
        if config.is_internal_shiboken_generator_build_and_part_of_top_level_all():
            return

        setuptools_install_prefix = sysconfig.get_paths()["purelib"]
        if OPTION["FINAL_INSTALL_PREFIX"]:
            setuptools_install_prefix = OPTION["FINAL_INSTALL_PREFIX"]
        log.info("=" * 30)
        log.info(f"Package version: {get_package_version()}")
        log.info(f"Build type:  {self.build_type}")
        log.info(f"Build tests: {self.build_tests}")
        log.info("-" * 3)
        log.info(f"Make path:      {self.make_path}")
        log.info(f"Make generator: {self.make_generator}")
        log.info(f"Make jobs:      {OPTION['JOBS']}")
        log.info("-" * 3)
        log.info(f"setup.py directory:           {self.script_dir}")
        log.info(f"Build scripts directory:      {build_scripts_dir}")
        log.info(f"Sources directory:            {self.sources_dir}")
        log.info(f"make build directory:         {self.build_dir}")
        log.info(f"make install directory:       {self.install_dir}")
        log.info(f"setuptools build directory:   {self.st_build_dir}")
        log.info(f"setuptools install directory: {setuptools_install_prefix}")
        log.info("-" * 3)
        log.info(f"Python executable: {self.py_executable}")
        log.info(f"Python includes:   {self.py_include_dir}")
        log.info(f"Python library:    {self.py_library}")
        log.info(f"Python prefix:     {self.py_prefix}")
        log.info(f"Python scripts:    {self.py_scripts_dir}")
        log.info(f"Python arch:       {self.py_arch}")

        log.info("-" * 3)
        log.info(f"Qt prefix:  {self.qtinfo.prefix_dir}")
        log.info(f"Qt qmake:   {self.qtinfo.qmake_command}")
        log.info(f"Qt qtpaths: {self.qtinfo.qtpaths_command}")
        log.info(f"Qt version: {self.qtinfo.version}")
        log.info(f"Qt bins:    {self.qtinfo.bins_dir}")
        log.info(f"Qt docs:    {self.qtinfo.docs_dir}")
        log.info(f"Qt plugins: {self.qtinfo.plugins_dir}")
        log.info("-" * 3)
        if sys.platform == 'win32':
            log.info(f"OpenSSL dll directory: {OPTION['OPENSSL']}")
        # for cross-compilation it is possible to use a macOS host, but
        # pyside_macos_deployment_target is not relevant for the target.
        # The only exception here is when we are trying to cross-compile from intel mac to m1 mac.
        # This case is not supported yet.
        if sys.platform == 'darwin' and not self.is_cross_compile:
            pyside_macos_deployment_target = (macos_pyside_min_deployment_target())
            log.info(f"MACOSX_DEPLOYMENT_TARGET set to: {pyside_macos_deployment_target}")
        log.info("=" * 30)

    def build_patchelf(self):
        if not sys.platform.startswith('linux'):
            return
        self._patchelf_path = which('patchelf')
        if self._patchelf_path:
            self._patchelf_path = Path(self._patchelf_path)
            if not self._patchelf_path.is_absolute():
                self._patchelf_path = Path.cwd() / self._patchelf_path
            log.info(f"Using {self._patchelf_path} ...")
            return
        else:
            raise SetupError("patchelf not found")

    def _enable_numpy(self):
        if OPTION["ENABLE_NUMPY_SUPPORT"] or OPTION["PYSIDE_NUMPY_SUPPORT"]:
            return True
        if OPTION["DISABLE_NUMPY_SUPPORT"]:
            return False
        if self.is_cross_compile:  # Do not search header in host Python
            return False
        # Debug builds require numpy to be built in debug mode on Windows
        # https://numpy.org/devdocs/user/troubleshooting-importerror.html
        return sys.platform != 'win32' or self.build_type.lower() != 'debug'

    def build_extension(self, extension):
        log.info(f"Building module {extension}...")

        # Prepare folders
        os.chdir(self.build_dir)
        module_build_dir = self.build_dir / extension
        skipflag_file = Path(f"{module_build_dir}-skip")
        if skipflag_file.exists():
            log.info(f"Skipping {extension} because {skipflag_file} exists")
            return

        module_src_dir = self._setup_build_dir(extension, module_build_dir)
        cmake_cmd = self._build_cmake_command(extension, module_src_dir)
        self._run_cmake_and_make(extension, cmake_cmd, module_src_dir)
        self._build_docs(extension)
        self._run_make_install(extension)
        os.chdir(self.script_dir)

    def _setup_build_dir(self, extension, module_build_dir):
        """Prepare the module build folder and return the module source dir."""
        if module_build_dir.exists():
            if not OPTION["REUSE_BUILD"]:
                log.info(f"Deleting module build folder {module_build_dir}...")
                try:
                    remove_tree(module_build_dir)
                except Exception as e:
                    log.error(f'***** problem removing "{module_build_dir}"')
                    log.error(f'ignored error: {e}')
            else:
                log.info(f"Reusing module build folder {module_build_dir}...")
        if not module_build_dir.exists():
            log.info(f"Creating module build folder {module_build_dir}...")
            os.makedirs(module_build_dir)
        os.chdir(module_build_dir)
        return self.sources_dir / extension

    def _build_cmake_command(self, extension, module_src_dir):
        """Construct and return the full CMake configuration command."""
        cmake_cmd = [str(OPTION["CMAKE"])]
        cmake_quiet_build = 1
        cmake_rule_messages = 0
        if OPTION["LOG_LEVEL"] == LogLevel.VERBOSE:
            # Pass a special custom option, to allow printing a lot less information when doing
            # a quiet build.
            cmake_quiet_build = 0
            if self.make_generator == "Unix Makefiles":
                # Hide progress messages for each built source file.
                # Doesn't seem to work if set within the cmake files themselves.
                cmake_rule_messages = 1

        if OPTION["UNITY"]:
            cmake_cmd.append("-DCMAKE_UNITY_BUILD=ON")
            batch_size = OPTION["UNITY_BUILD_BATCH_SIZE"]
            cmake_cmd.append(f"-DCMAKE_UNITY_BUILD_BATCH_SIZE={batch_size}")
            log.info("Using UNITY build")

        if OPTION['SHIBOKEN_FORCE_PROCESS_SYSTEM_HEADERS']:
            cmake_cmd.append("-DPYSIDE_TREAT_QT_INCLUDE_DIRS_AS_NON_SYSTEM=ON")
            log.info("Shiboken will now process system Qt headers")

        if OPTION['SHIBOKEN_EXTRA_INCLUDE_PATHS']:
            extra_include_paths = ';'.join(OPTION['SHIBOKEN_EXTRA_INCLUDE_PATHS'].split(','))
            cmake_cmd.append(f"-DSHIBOKEN_FORCE_PROCESS_SYSTEM_INCLUDE_PATHS={extra_include_paths}")
            log.info(f"Shiboken will now process system headers from: {extra_include_paths}")

        cmake_cmd += [
            "-G", self.make_generator,  # type: ignore[list-item]
            f"-DBUILD_TESTS={self.build_tests}",
            f"-DQt5Help_DIR={self.qtinfo.docs_dir}",
            f"-DCMAKE_BUILD_TYPE={self.build_type}",
            f"-DCMAKE_INSTALL_PREFIX={self.install_dir}",
            # Record the minimum/maximum Python version for later use in Shiboken.__init__
            f"-DMINIMUM_PYTHON_VERSION={get_allowed_python_versions()[0]}",
            f"-DMAXIMUM_PYTHON_VERSION={get_allowed_python_versions()[-1]}",
            f"-DQUIET_BUILD={cmake_quiet_build}",
            f"-DCMAKE_RULE_MESSAGES={cmake_rule_messages}",
            str(module_src_dir)
        ]

        # When cross-compiling we set Python_ROOT_DIR to tell
        # FindPython.cmake where to pick up the device python libs.
        if self.is_cross_compile:
            if self.python_target_path:
                cmake_cmd.append(f"-DPython_ROOT_DIR={self.python_target_path}")

            # Host python is needed when cross compiling to run
            # embedding_generator.py. Pass it as a separate option.
            cmake_cmd.append(f"-DQFP_PYTHON_HOST_PATH={sys.executable}")
        else:
            cmake_cmd.append(f"-DPython_EXECUTABLE={self.py_executable}")
            cmake_cmd.append(f"-DPython_INCLUDE_DIR={self.py_include_dir}")
            cmake_cmd.append(f"-DPython_LIBRARY={self.py_library}")

        # If a custom shiboken cmake config directory path was provided, pass it to CMake.
        if OPTION["SHIBOKEN_CONFIG_DIR"] and config.is_internal_pyside_build():
            config_dir = OPTION["SHIBOKEN_CONFIG_DIR"]
            if config_dir.exists():
                log.info(f"Using custom provided {SHIBOKEN} installation: {config_dir}")
                cmake_cmd.append(f"-DShiboken6_DIR={config_dir}")
            else:
                log.info(f"Custom provided {SHIBOKEN} installation not found. "
                         f"Path given: {config_dir}")

        if OPTION["MODULE_SUBSET"]:
            cmake_cmd.append(f"-DMODULES={parse_modules(OPTION['MODULE_SUBSET'])}")

        if OPTION["SKIP_MODULES"]:
            cmake_cmd.append(f"-DSKIP_MODULES={parse_modules(OPTION['SKIP_MODULES'])}")

        # Add source location for generating documentation
        if self.qt_src_dir:
            cmake_cmd.append(f"-DQT_SRC_DIR={self.qt_src_dir}")
        if OPTION['NO_QT_TOOLS']:
            cmake_cmd.append("-DNO_QT_TOOLS=yes")
        if OPTION['SKIP_DOCS']:
            log.info("Warning: '--skip-docs' is deprecated and will be removed. "
                     "The documentation is not built by default")
        if OPTION['BUILD_DOCS']:
            cmake_cmd.append("-DBUILD_DOCS=yes")
        log.info(f"Qt Source dir: {self.qt_src_dir}")

        # Use Legacy OpenGL to avoid issues on systems like Ubuntu 20.04
        # which require to manually install the libraries which
        # were previously linked to the QtGui module in 6.1 (QTBUG-89754).
        cmake_cmd.append("-DOpenGL_GL_PREFERENCE=LEGACY")

        if OPTION['AVOID_PROTECTED_HACK']:
            cmake_cmd.append("-DAVOID_PROTECTED_HACK=1")

        if self._enable_numpy():
            numpy = get_numpy_location()
            if numpy:
                cmake_cmd.append(f"-DNUMPY_INCLUDE_DIR={numpy}")
            else:
                log.warning('numpy include directory was not found.')

        if self.build_type.lower() != 'debug':
            if OPTION['NO_STRIP']:
                cmake_cmd.append("-DQFP_NO_STRIP=1")
            if OPTION['NO_OVERRIDE_OPTIMIZATION_FLAGS']:
                cmake_cmd.append("-DQFP_NO_OVERRIDE_OPTIMIZATION_FLAGS=1")

        if not OPTION["LIMITED_API"]:
            if sys.platform == 'win32' and self.debug:
                cmake_cmd.append("-DFORCE_LIMITED_API=no")
        else:
            if OPTION["LIMITED_API"].lower() in ("yes", "y", "1", "true"):
                cmake_cmd.append("-DFORCE_LIMITED_API=yes")
            elif OPTION["LIMITED_API"].lower() in ("no", "n", "0", "false"):
                cmake_cmd.append("-DFORCE_LIMITED_API=no")
            else:
                raise SetupError(
                    "Option '--limited-api' must be 'yes' or 'no'."
                    f"Default is yes if Python version >= {get_allowed_python_versions()[0]} "
                    "and Release build on Windows"
                )

        if OPTION["DISABLE_PYI"]:
            cmake_cmd.append("-DDISABLE_PYI=yes")

        if OPTION["UNOPTIMIZE"]:
            value = OPTION["UNOPTIMIZE"]
            cmake_cmd.append(f"-DSHIBOKEN_UNOPTIMIZE={value}")

        if OPTION["LOG_LEVEL"] == LogLevel.VERBOSE:
            cmake_cmd.append("-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON")
        else:
            cmake_cmd.append("-DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF")

        if OPTION['COMPILER_LAUNCHER']:
            compiler_launcher = OPTION['COMPILER_LAUNCHER']
            cmake_cmd.append(f"-DCMAKE_C_COMPILER_LAUNCHER={compiler_launcher}")
            cmake_cmd.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={compiler_launcher}")

        if OPTION["SBOM"]:
            cmake_cmd.append("-DQT_GENERATE_SBOM=ON")
            cmake_cmd.append("-DQT_SBOM_GENERATE_SPDX_V2_JSON=ON")
            cmake_cmd.append("-DQT_SBOM_REQUIRE_GENERATE_SPDX_V2_JSON=ON")
            cmake_cmd.append("-DQT_SBOM_GENERATE_CYDX_V1_6=ON")
            cmake_cmd.append("-DQT_SBOM_REQUIRE_GENERATE_CYDX_V1_6=ON")

        if OPTION["SANITIZE_ADDRESS"]:
            cmake_cmd.append("-DSANITIZE_ADDRESS=ON")
        if OPTION["SANITIZE_THREAD"]:
            # Some simple sanity checking. Only use at your own risk.
            if sys.platform == "win32" and not self.is_cross_compile:
                self.warn("Thread sanitizer may not be supported yet.")
            cmake_cmd.append("-DSANITIZE_THREAD=ON")

        if extension.lower() == PYSIDE:
            pyside_qt_conf_prefix = ''
            if OPTION["QT_CONF_PREFIX"]:
                pyside_qt_conf_prefix = OPTION["QT_CONF_PREFIX"]
            else:
                if OPTION["STANDALONE"]:
                    pyside_qt_conf_prefix = '"Qt"'
                if sys.platform == 'win32':
                    pyside_qt_conf_prefix = '"."'
            cmake_cmd.append(f"-DPYSIDE_QT_CONF_PREFIX={pyside_qt_conf_prefix}")

        if OPTION["STANDALONE"]:
            cmake_cmd.append("-DSTANDALONE:BOOL=ON")

        # Pass package version to CMake, so this string can be
        # embedded into _config.py file.
        package_version = get_package_version()
        cmake_cmd.append(f"-DPACKAGE_SETUP_PY_PACKAGE_VERSION={package_version}")

        # In case if this is a snapshot build, also pass the
        # timestamp as a separate value, because it is the only
        # version component that is actually generated by setup.py.
        timestamp = ''
        if OPTION["SNAPSHOT_BUILD"]:
            timestamp = get_package_timestamp()
        cmake_cmd.append(f"-DPACKAGE_SETUP_PY_PACKAGE_TIMESTAMP={timestamp}")

        if extension.lower() in [SHIBOKEN]:
            cmake_cmd.append("-DUSE_PYTHON_VERSION=3.10")

        cmake_cmd += platform_cmake_options()

        # for a macOS host, cross-compilation is possible, but for the host system as such
        # we only build shiboken. Hence the following code can be skipped.
        if sys.platform == 'darwin' and not self.is_cross_compile:
            if OPTION["MACOS_ARCH"]:
                # also tell cmake which architecture to use
                cmake_cmd.append(f"-DCMAKE_OSX_ARCHITECTURES:STRING={OPTION['MACOS_ARCH']}")

            if OPTION["MACOS_USE_LIBCPP"]:
                # Explicitly link the libc++ standard library (useful
                # for macOS deployment targets lower than 10.9).
                # This is not on by default, because most libraries and
                # executables on macOS <= 10.8 are linked to libstdc++,
                # and mixing standard libraries can lead to crashes.
                # On macOS >= 10.9 with a similar minimum deployment
                # target, libc++ is linked in implicitly, thus the
                # option is a no-op in those cases.
                cmake_cmd.append("-DOSX_USE_LIBCPP=ON")

            if OPTION["MACOS_SYSROOT"]:
                cmake_cmd.append(f"-DCMAKE_OSX_SYSROOT={OPTION['MACOS_SYSROOT']}")
            else:
                latest_sdk_path = run_process_output(['xcrun', '--sdk', 'macosx',
                                                      '--show-sdk-path'])
                if latest_sdk_path:
                    latest_sdk_path = latest_sdk_path[0]
                    cmake_cmd.append(f"-DCMAKE_OSX_SYSROOT={latest_sdk_path}")

            # Set macOS minimum deployment target (version).
            # This is required so that calling
            #   run_process -> subprocess.call()
            # does not set its own minimum deployment target
            # environment variable which is based on the python
            # interpreter sysconfig value.
            # Doing so could break the detected clang include paths
            # for example.
            deployment_target = macos_pyside_min_deployment_target()
            cmake_cmd.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}")
            os.environ['MACOSX_DEPLOYMENT_TARGET'] = deployment_target

        if OPTION["BUILD_DOCS"]:
            # Build the whole documentation (Base + API) by default
            cmake_cmd.append("-DFULLDOCSBUILD=1")

            if OPTION["DOC_BUILD_ONLINE"]:
                log.info("Output format will be HTML")
                cmake_cmd.append("-DDOC_OUTPUT_FORMAT=html")
            else:
                log.info("Output format will be qthelp")
                cmake_cmd.append("-DDOC_OUTPUT_FORMAT=qthelp")
        else:
            cmake_cmd.append("-DBUILD_DOCS=no")
            if OPTION["DOC_BUILD_ONLINE"]:
                log.info("Warning: Documentation build is disabled, "
                         "however --doc-build-online was passed. "
                         "Use '--build-docs' to enable the documentation build")

        if OPTION["PYSIDE_NUMPY_SUPPORT"]:
            log.info("Warning: '--pyside-numpy-support' is deprecated and will be removed. "
                     "Use --enable-numpy-support/--disable-numpy-support.")

        target_qt_prefix_path = self.qtinfo.prefix_dir
        cmake_cmd.append(f"-DQFP_QT_TARGET_PATH={target_qt_prefix_path}")
        if self.qt_host_path:
            cmake_cmd.append(f"-DQFP_QT_HOST_PATH={self.qt_host_path}")

        # Use the qdoc of the selected Qt (not the first one in PATH).
        if self.qt_host_path:
            qdoc_dirs = [Path(self.qt_host_path) / "bin", Path(self.qt_host_path) / "libexec"]
        else:
            qdoc_dirs = [Path(self.qtinfo.bins_dir), Path(self.qtinfo.lib_execs_dir)]
        qdoc_path = next((d / "qdoc" for d in qdoc_dirs if (d / "qdoc").is_file()), None)
        if qdoc_path:
            cmake_cmd.append(f"-DQDOC_EXECUTABLE={qdoc_path}")

        if self.is_cross_compile and (not OPTION["SHIBOKEN_HOST_PATH"]
                                      or not Path(OPTION["SHIBOKEN_HOST_PATH"]).exists()):
            raise SetupError("Please specify the location of host shiboken tools via "
                             "--shiboken-host-path=")

        if self.shiboken_host_path:
            cmake_cmd.append(f"-DQFP_SHIBOKEN_HOST_PATH={self.shiboken_host_path}")

        if self.shiboken_target_path:
            cmake_cmd.append(f"-DQFP_SHIBOKEN_TARGET_PATH={self.shiboken_target_path}")
        elif extension.lower() != SHIBOKEN:
            # Always point cmake to the project's own shiboken install so the
            # system-installed shiboken is not picked up, regardless of whether
            # this is a cross-compile build or a native build.
            cmake_cmd.append(f"-DQFP_SHIBOKEN_TARGET_PATH={self.install_dir}")

        if OPTION["SKIP_MYPY_TEST"]:
            cmake_cmd.append("-DSKIP_MYPY_TEST=1")

        if self.cmake_toolchain_file:
            cmake_cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={self.cmake_toolchain_file}")

        return cmake_cmd

    def _run_cmake_and_make(self, extension, cmake_cmd, module_src_dir):
        """Configure with CMake then compile with make/ninja."""
        if not OPTION["SKIP_CMAKE"]:
            log.info(f"Configuring module {extension} ({module_src_dir})...")
            if run_process(cmake_cmd) != 0:
                raise SetupError(f"Error configuring {extension}")
        else:
            log.info(f"Reusing old configuration for module {extension} ({module_src_dir})...")

        log.info(f"-- Compiling module {extension}...")
        cmd_make = [str(self.make_path)]
        if OPTION["JOBS"]:
            cmd_make.append(OPTION["JOBS"])
        if OPTION["LOG_LEVEL"] == LogLevel.VERBOSE and self.make_generator == "Ninja":
            cmd_make.append("-v")
        if run_process(cmd_make) != 0:
            raise SetupError(f"Error compiling {extension}")

    def _run_doc_build(self, extension, target="doc"):
        log.info(f"Generating {extension} documentation")
        make_doc_cmd = [str(self.make_path), target]
        if OPTION["LOG_LEVEL"] == LogLevel.VERBOSE and self.make_generator == "Ninja":
            make_doc_cmd.append("-v")
        if run_process(make_doc_cmd) != 0:
            raise SetupError(f"Error generating documentation for {extension}")

    def _build_docs(self, extension):
        """Optionally build Sphinx documentation for the given extension."""
        if OPTION["BUILD_DOCS"]:
            if extension.lower() == SHIBOKEN_GENERATOR:
                found = importlib.util.find_spec("sphinx")
                if found:
                    self._run_doc_build(extension)
                else:
                    log.info("Sphinx not found, skipping documentation build")
            elif extension.lower() == SHIBOKEN:
                self._run_doc_build(extension, "libshiboken_qdoc")
            elif extension.lower() == PYSIDE:
                doc_targets = ["libpyside_qdoc", "libpysideqml_qdoc"]
                if self.qtinfo.find_tool("repc"):
                    doc_targets.append("libpysideremoteobjects_qdoc")
                for target in doc_targets:
                    self._run_doc_build(extension, target)
        else:
            log.info("-- Skipped documentation generation. Enable with '--build-docs'")

    def _run_make_install(self, extension):
        """Run make install, or skip if SKIP_MAKE_INSTALL is set."""
        if not OPTION["SKIP_MAKE_INSTALL"]:
            log.info(f"Installing module {extension}...")
            # Need to wait a second, so installed file timestamps are
            # older than build file timestamps.
            # See https://gitlab.kitware.com/cmake/cmake/issues/16155
            # for issue details.
            if sys.platform == 'darwin':
                log.info("Waiting 1 second, to ensure installation is successful...")
                time.sleep(1)
            # ninja: error: unknown target 'install/fast'
            target = 'install/fast' if self.make_generator != 'Ninja' else 'install'
            if run_process([str(self.make_path), target]) != 0:
                raise SetupError(f"Error pseudo installing {extension}")
        else:
            log.info(f"Skipped installing module {extension}")

    def prepare_packages(self):
        """
        This will copy all relevant files from the various locations in the "cmake install dir",
        to the setup tools build dir (which is read from self.build_lib provided by setuptools).

        After that setuptools.command.build_py is smart enough to copy everything
        from the build dir to the install dir (the virtualenv site-packages for example).
        """
        try:
            log.info("Preparing setup tools build directory.")
            _vars = {
                "site_packages_dir": self.site_packages_dir,
                "sources_dir": self.sources_dir,
                "install_dir": self.install_dir,
                "build_dir": self.build_dir,
                "script_dir": self.script_dir,
                "st_build_dir": self.st_build_dir,
                "cmake_package_name": config.package_name(),
                "st_package_name": config.package_name(),
                "ssl_libs_dir": OPTION["OPENSSL"],
                "py_version": self.py_version,
                "qt_version": self.qtinfo.version,
                "qt_bin_dir": self.qtinfo.bins_dir,
                "qt_data_dir": self.qtinfo.data_dir,
                "qt_doc_dir": self.qtinfo.docs_dir,
                "qt_lib_dir": self.qtinfo.libs_dir,
                "qt_module_json_files_dir": self.qtinfo.module_json_files_dir,
                "qt_metatypes_dir": self.qtinfo.metatypes_dir,
                "qt_lib_execs_dir": self.qtinfo.lib_execs_dir,
                "qt_plugins_dir": self.qtinfo.plugins_dir,
                "qt_prefix_dir": self.qtinfo.prefix_dir,
                "qt_translations_dir": self.qtinfo.translations_dir,
                "qt_qml_dir": self.qtinfo.qml_dir,

                # TODO: This is currently None when cross-compiling
                # There doesn't seem to be any place where we can query
                # it. Fortunately it's currently only used when
                # packaging Windows vcredist.
                "target_arch": self.py_arch,
            }

            # Needed for correct file installation in generator build
            # case.
            if config.is_internal_shiboken_generator_build():
                _vars['cmake_package_name'] = config.shiboken_module_option_name

            os.chdir(self.script_dir)

            # Clean up the previous st_build_dir before files are copied
            # into it again. That's the because the same dir is used
            # when copying the files for each of the sub-projects and
            # we don't want to accidentally install shiboken files
            # as part of pyside-tools package.
            if self.st_build_dir.is_dir():
                log.info(f"Removing {self.st_build_dir}")
                try:
                    remove_tree(self.st_build_dir)
                except Exception as e:
                    log.warning(f'problem removing "{self.st_build_dir}"')
                    log.warning(f'ignored error: {e}')

            if sys.platform == "win32":
                _vars['dbg_postfix'] = OPTION["DEBUG"] and "_d" or ""
                return prepare_packages_win32(self, _vars)
            else:
                return prepare_packages_posix(self, _vars, self.is_cross_compile)
        except IOError as e:
            print('setup.py/prepare_packages: ', e)
            raise

    def qt_is_framework_build(self):
        return Path(f"{self.qtinfo.headers_dir}/../lib/QtCore.framework").is_dir()

    def get_built_pyside_config(self, _vars):
        # Get config that contains list of built modules, and
        # SOVERSIONs of the built libraries.
        st_build_dir = Path(_vars['st_build_dir'])
        config_path = st_build_dir / config.package_name() / "_config.py"
        temp_config = get_python_dict(config_path)
        if 'built_modules' not in temp_config:
            temp_config['built_modules'] = []
        return temp_config

    def is_webengine_built(self, built_modules):
        return ('WebEngineWidgets' in built_modules
                or 'WebEngineCore' in built_modules
                or 'WebEngine' in built_modules)

    def prepare_standalone_clang(self, is_win=False):
        """
        Copies the libclang library to the shiboken6-generator
        package so that the shiboken executable works.
        """
        log.info('Finding path to the libclang shared library.')
        cmake_cmd = [
            str(OPTION["CMAKE"]),
            "-L",         # Lists variables
            "-N",         # Just inspects the cache (faster)
            "-B",         # Specifies the build dir
            str(self.shiboken_generator_build_dir)
        ]
        out = run_process_output(cmake_cmd)
        lines = [s.strip() for s in out]
        pattern = re.compile(r"CLANG_LIBRARY:FILEPATH=(.+)$")

        clang_lib_path = None
        for line in lines:
            match = pattern.search(line)
            if match:
                clang_lib_path = match.group(1)
                break

        if not clang_lib_path:
            raise RuntimeError("Could not find the location of the libclang "
                               "library inside the CMake cache file.")

        if is_win:
            # clang_lib_path points to the static import library
            # (lib/libclang.lib), whereas we want to copy the shared
            # library (bin/libclang.dll).
            clang_lib_path = Path(re.sub(r'lib/libclang.lib$',
                                         'bin/libclang.dll',
                                         clang_lib_path))
        else:
            clang_lib_path = Path(clang_lib_path)
            # shiboken6 links against libclang.so.6 or a similarly
            # named library.
            # If the linked against library is a symlink, resolve
            # the symlink once (but not all the way to the real
            # file) on Linux and macOS,
            # so that we get the path to the "SO version" symlink
            # (the one used as the install name in the shared library
            # dependency section).
            # E.g. On Linux libclang.so -> libclang.so.6 ->
            # libclang.so.6.0.
            # "libclang.so.6" is the name we want for the copied file.
            if clang_lib_path.is_symlink():
                link_target = Path(os.readlink(clang_lib_path))
                if link_target.is_absolute():
                    clang_lib_path = link_target
                else:
                    # link_target is relative, transform to absolute.
                    clang_lib_path = clang_lib_path.parent / link_target
            clang_lib_path = clang_lib_path.resolve()

        # The destination will be the shiboken package folder.
        _vars = {}
        _vars['st_build_dir'] = self.st_build_dir
        _vars['st_package_name'] = config.package_name()
        destination_dir = Path("{st_build_dir}/{st_package_name}".format(**_vars))

        if clang_lib_path.exists():
            basename = clang_lib_path.name
            # In case of static libclang we don't need the lib file inside the wheel
            if '.a' == clang_lib_path.suffix:
                log.info("Skip copying libclang archive to the package.")
                return

            log.info(f"Copying libclang shared library {clang_lib_path} to the package "
                     f"folder as {basename}.")
            destination_path = destination_dir / basename

            # It is possible that the resolved libclang has a different SONAME
            # For example the actual libclang might be named libclang.so.14.0.0 and its
            # SONAME might be libclang.so.13
            # In this case, the ideal approach is to find the SONAME and create a symlink to the
            # actual libclang in the destination directory. But, Python packaging (setuptools)
            # does not support symlinks.
            # So, we rename the actual libclang to the SONAME and copy it to the destination
            if sys.platform == 'linux':
                soname = get_soname(clang_lib_path)
                if soname and soname != clang_lib_path.name:
                    destination_path = destination_path.parent / soname

            # Need to modify permissions in case file is not writable
            # (a reinstall would cause a permission denied error).
            copyfile(clang_lib_path,
                     destination_path,
                     force_copy_symlink=True,
                     make_writable_by_owner=True)
        else:
            raise RuntimeError("Error copying libclang library "
                               f"from {clang_lib_path} to {destination_dir}. ")

    def get_shared_library_filters(self):
        unix_filters = ["*.so", "*.so.*"]
        darwin_filters = ["*.so", "*.dylib"]
        filters = []
        if self.is_cross_compile:
            if 'darwin' in self.plat_name or 'macos' in self.plat_name:
                filters = darwin_filters
            elif 'linux' in self.plat_name or 'android' in self.plat_name:
                filters = unix_filters
            else:
                log.warning(f"No shared library filters found for platform {self.plat_name}. "
                            f"The package might miss Qt libraries and plugins.")
        else:
            if sys.platform == 'darwin':
                filters = darwin_filters
            else:
                filters = unix_filters
        return filters

    def _find_shared_libraries(self, path, recursive=False):
        """Helper to find shared libraries in a path."""
        result = set()
        for filter in self.get_shared_library_filters():
            glob_pattern = f"**/{filter}" if recursive else filter
            for library in path.glob(glob_pattern):
                result.add(library)
        return list(result)

    def package_libraries(self, package_path):
        """Returns the libraries of the Python module"""
        return self._find_shared_libraries(package_path)

    def get_shared_libraries_in_path_recursively(self, initial_path):
        """Returns shared library plugins in given path (collected
        recursively)"""
        return self._find_shared_libraries(initial_path, recursive=True)

    def update_rpath(self, executables, libexec=False):
        ROOT = '@loader_path' if sys.platform == 'darwin' else '$ORIGIN'
        QT_PATH = '/../lib' if libexec else '/Qt/lib'

        message = "Patched rpath to '$ORIGIN/' in"
        if sys.platform.startswith('linux'):

            def rpath_cmd(srcpath):
                final_rpath = ''
                # Command line rpath option takes precedence over
                # automatically added one.
                if OPTION["RPATH_VALUES"]:
                    final_rpath = OPTION["RPATH_VALUES"]
                else:
                    # Add rpath values pointing to $ORIGIN and the
                    # installed qt lib directory.
                    final_rpath = self.qtinfo.libs_dir
                    if OPTION["STANDALONE"]:
                        final_rpath = f'{ROOT}{QT_PATH}'
                override = OPTION["STANDALONE"]
                linux_fix_rpaths_for_library(self._patchelf_path, srcpath, final_rpath,
                                             override=override)

        elif sys.platform == 'darwin':
            message = "Updated rpath in"

            def rpath_cmd(srcpath):
                final_rpath = ''
                # Command line rpath option takes precedence over
                # automatically added one.
                if OPTION["RPATH_VALUES"]:
                    final_rpath = OPTION["RPATH_VALUES"]
                else:
                    if OPTION["STANDALONE"]:
                        final_rpath = f'{ROOT}{QT_PATH}'
                    else:
                        final_rpath = self.qtinfo.libs_dir
                macos_fix_rpaths_for_library(srcpath, final_rpath)

        else:
            raise RuntimeError(f"Not configured for platform {sys.platform}")

        # Update rpath
        for executable in executables:
            if executable.is_dir() or executable.is_symlink():
                continue
            if not executable.exists():
                continue
            rpath_cmd(executable)
            log.debug(f"{message} {executable}.")

    def update_rpath_for_linux(self, target_type: RpathTargetType, paths=None, qt_lib_dir=None):
        # If the linux sysroot (where the plugins/libraries are copied from)
        # is from a mainline distribution, it might have a different
        # directory layout than the one we expect to have in the wheel.
        # We have to ensure that any copied files have rpath values that
        # can find Qt libs in the newly assembled wheel dir layout.
        # Especially important for ICU libs, so that they don't
        # accidentally load dependencies from the system.
        if not (self.is_cross_compile and sys.platform.startswith('linux') and self.standalone):
            return

        if target_type == RpathTargetType.qt_libraries:
            qt_lib_dir = Path(qt_lib_dir)
            log.info(f"Patching rpath for Qt and ICU libraries in {qt_lib_dir}.")
            file_list = self.package_libraries(qt_lib_dir)
            fixed_rpath: str | None = "$ORIGIN"
        else:
            log.info("Patching rpath for Qt and QML plugins.")
            file_list = paths
            fixed_rpath = "$ORIGIN/../../lib" if target_type == RpathTargetType.plugins else None

        for item in file_list:
            if item.is_dir() or item.is_symlink():
                continue
            if not item.exists():
                continue

            if target_type == RpathTargetType.qml_plugins:
                plugin_dir = item.parent
                # FIXME: there is no os.path.relpath equivalent on pathlib.
                # The Path.relative_to is not equivalent and raises ValueError when the paths
                # are not subpaths, so it doesn't generate "../../something".
                rel_path = os.path.relpath(qt_lib_dir, plugin_dir)
                rpath_value = Path("$ORIGIN") / rel_path
            else:
                assert fixed_rpath is not None
                rpath_value = fixed_rpath

            linux_fix_rpaths_for_library(self._patchelf_path, item, rpath_value, override=True)
            log.debug(f"Patched rpath to '{rpath_value}' in {item}.")


class PysideBaseDocs(Command, CommandMixin):
    description = "Build the base documentation only"
    user_options = CommandMixin.mixin_user_options

    def __init__(self, *args, **kwargs):
        self.command_name = "build_base_docs"
        Command.__init__(self, *args, **kwargs)
        CommandMixin.__init__(self)

    def initialize_options(self):
        log.info("-- This build process will not include the API documentation. "
                 "API documentation requires a full build of pyside/shiboken.")
        self.skip = False
        if config.is_internal_shiboken_module_build():
            self.skip = True
        if not self.skip:
            self.name = config.package_name().lower()
            self.doc_dir = config.setup_script_dir / "sources" / self.name / "doc"
            # Check if sphinx is installed to proceed.
            found = importlib.util.find_spec("sphinx")
            self.html_dir = Path("html")
            if found:
                if self.name == SHIBOKEN_GENERATOR:
                    # Delete the 'html' directory since new docs will be generated anyway
                    if self.html_dir.is_dir():
                        rmtree(self.html_dir)
                        log.info("-- Deleted old html directory")
                    log.info("-- Generating Shiboken Generator documentation")
                    log.info(f"-- Documentation directory: 'html/{PYSIDE}/{SHIBOKEN_GENERATOR}/'")
                elif self.name == PYSIDE:
                    log.info("-- Generating PySide documentation")
                    log.info(f"-- Documentation directory: 'html/{PYSIDE}/'")
            else:
                raise SetupError("Sphinx not found - aborting")

            # creating directories html/pyside6/shiboken6
            try:
                if not self.html_dir.is_dir():
                    self.html_dir.mkdir(parents=True)
                if self.name == SHIBOKEN_GENERATOR:
                    out_pyside = self.html_dir / PYSIDE
                    if not out_pyside.is_dir():
                        out_pyside.mkdir(parents=True)
                    out_shiboken = out_pyside / SHIBOKEN_GENERATOR
                    if not out_shiboken.is_dir():
                        out_shiboken.mkdir(parents=True)
                    self.out_dir = out_shiboken
                # We know that on the shiboken step, we already created the
                # 'pyside6' directory
                elif self.name == PYSIDE:
                    self.out_dir = self.html_dir / PYSIDE
            except (PermissionError, FileExistsError):
                raise SetupError(f"Error while creating directories for {self.doc_dir}")

    def run(self):
        if not self.skip:
            cmake_cmd = [
                str(OPTION["CMAKE"]),
                "-S", str(self.doc_dir),
                "-B", str(self.out_dir),
                "-DDOC_OUTPUT_FORMAT=html",
                "-DFULLDOCSBUILD=0",
            ]

            cmake_quiet_build = 1
            cmake_message_log_level = "STATUS"

            # Define log level
            if OPTION["LOG_LEVEL"] == LogLevel.VERBOSE:
                cmake_quiet_build = 0
                cmake_message_log_level = "VERBOSE"
            elif OPTION["LOG_LEVEL"] == LogLevel.QUIET:
                cmake_message_log_level = "ERROR"

            cmake_cmd.append(f"-DQUIET_BUILD={cmake_quiet_build}")
            cmake_cmd.append(f"-DCMAKE_MESSAGE_LOG_LEVEL={cmake_message_log_level}")

            if run_process(cmake_cmd) != 0:
                raise SetupError(f"Error running CMake for {self.doc_dir}")

            if self.name == PYSIDE:
                def run_script(script_path, args=None):
                    cmd = [sys.executable, os.fspath(script_path)]
                    if args:
                        cmd.extend(args)
                    if run_process(cmd) != 0:
                        raise SetupError(f"Error running {script_path}")

                self.sphinx_src = self.out_dir / "base"
                # Generates the .rst files from the examples
                example_gallery = config.setup_script_dir / "tools" / "example_gallery" / "main.py"
                if example_gallery.is_file():
                    example_gallery_args = []
                    if OPTION["LOG_LEVEL"] == LogLevel.QUIET:
                        example_gallery_args.append("--quiet")
                    qt_src = OPTION['QT_SRC']
                    if qt_src:
                        example_gallery_args.extend(["--qt-src-dir", qt_src])
                    run_script(example_gallery, example_gallery_args)
                else:
                    log.warning("Example gallery script for generating .rst for examples"
                                f"not found: {example_gallery}")

                # Generates the .rst files from the release notes
                release_notes = config.setup_script_dir / "tools" / "release_notes" / "main.py"
                if release_notes.is_file():
                    release_notes_args = []
                    if OPTION["LOG_LEVEL"] != LogLevel.QUIET:
                        release_notes_args.append("--verbose")
                    run_script(release_notes, release_notes_args)
                else:
                    log.warning("Release notes script for generating .rst for release notes"
                                f"not found: {release_notes}")
            elif self.name == SHIBOKEN_GENERATOR:
                self.sphinx_src = self.out_dir

            sphinx_cmd = ["sphinx-build", "-b", "html", "-j", "auto", "-n", "-c",
                          str(self.sphinx_src), str(self.doc_dir),
                          str(self.out_dir)]
            if run_process(sphinx_cmd) != 0:
                raise SetupError(f"Error running CMake for {self.doc_dir}")
        # Last message
        if not self.skip and self.name == PYSIDE:
            log.info(f"-- The documentation was built. Check html/{PYSIDE}/index.html")

    def finalize_options(self):
        CommandMixin.mixin_finalize_options(self)


cmd_class_dict = {
    'build': PysideBuild,
    'build_py': PysideBuildPy,
    'build_ext': PysideBuildExt,
    'bdist_egg': PysideBdistEgg,
    'develop': PysideDevelop,
    'install': PysideInstall,
    'install_lib': PysideInstallLib,
    'build_base_docs': PysideBaseDocs,
}
if wheel_module_exists:
    pyside_bdist_wheel = get_bdist_wheel_override()
    if pyside_bdist_wheel:
        cmd_class_dict['bdist_wheel'] = pyside_bdist_wheel
