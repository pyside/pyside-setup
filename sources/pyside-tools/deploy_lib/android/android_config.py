# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
# Qt-Security score:critical reason:handling-untrusted-data
from __future__ import annotations
import re
import tempfile
import logging
import zipfile
import xml.etree.ElementTree as ET

from pathlib import Path
from pkginfo import Wheel

from . import (extract_and_copy_jar, get_wheel_android_arch, find_lib_dependencies,
               get_llvm_readobj, find_qtlibs_in_wheel, platform_map, create_recipe,
               ANDROID_DEPLOY_CACHE, PYSIDE_ONLY_MODULES)
from .. import Config, get_all_pyside_modules, MAJOR_VERSION, safe_extractall
from .android_utilities import ANDROID_NDK_VERSION, download_android_ndk


class AndroidConfig(Config):
    """
    Wrapper class around pysidedeploy.spec file for pyside6-android-deploy
    """
    def __init__(self, config_file: Path, source_file: Path, dry_run: bool,
                 android_data, existing_config_file: bool = False,
                 extra_ignore_dirs: list[str] = None, name: str = None):
        super().__init__(config_file=config_file, source_file=source_file,
                         dry_run=dry_run, existing_config_file=existing_config_file, name=name)

        self.extra_ignore_dirs = extra_ignore_dirs

        if android_data.wheel_pyside:
            self.wheel_pyside = android_data.wheel_pyside
        else:
            wheel_pyside_temp = self.get_value("android", "wheel_pyside")
            if not wheel_pyside_temp:
                raise RuntimeError("[DEPLOY] Unable to find PySide6 Android wheel")
            self.wheel_pyside = Path(wheel_pyside_temp).resolve()

        if android_data.wheel_shiboken:
            self.wheel_shiboken = android_data.wheel_shiboken
        else:
            wheel_shiboken_temp = self.get_value("android", "wheel_shiboken")
            if not wheel_shiboken_temp:
                raise RuntimeError("[DEPLOY] Unable to find shiboken6 Android wheel")
            self.wheel_shiboken = Path(wheel_shiboken_temp).resolve()

        self.ndk_path = None
        if android_data.ndk_path:
            # from cli
            self.ndk_path = android_data.ndk_path
        else:
            # from config
            ndk_path_temp = self.get_value("buildozer", "ndk_path")
            if ndk_path_temp:
                self.ndk_path = Path(ndk_path_temp)
            else:
                ndk_path_temp = (ANDROID_DEPLOY_CACHE / "android-ndk"
                                 / f"android-ndk-r{ANDROID_NDK_VERSION}")
                if ndk_path_temp.exists():
                    self.ndk_path = ndk_path_temp
                else:
                    # download NDK
                    if not ANDROID_DEPLOY_CACHE.exists():
                        ANDROID_DEPLOY_CACHE.mkdir()
                        logging.info(f"Cache created at {str(ANDROID_DEPLOY_CACHE.resolve())}")

                    logging.info("[DEPLOY] Downloading Android NDK")
                    self.ndk_path = download_android_ndk(ANDROID_DEPLOY_CACHE)

        if not self.ndk_path:
            raise RuntimeError(
                "[DEPLOY] Unable to find the Android NDK. Pass it with --ndk-path, or set "
                "ndk_path in the [buildozer] section of pysidedeploy.spec. The NDK is "
                "required to inspect the Qt libraries in the wheel."
            )

        self.sdk_path = None
        if android_data.sdk_path:
            self.sdk_path = android_data.sdk_path
        else:
            sdk_path_temp = self.get_value("buildozer", "sdk_path")
            if sdk_path_temp:
                self.sdk_path = Path(sdk_path_temp)
            else:
                sdk_path_temp = ANDROID_DEPLOY_CACHE / "android-sdk"
                if sdk_path_temp.exists():
                    self.sdk_path = sdk_path_temp
                else:
                    logging.info("[DEPLOY] Use default SDK from buildozer")

        if self.sdk_path:
            print(f"Using Android SDK: {str(self.sdk_path)}")

        recipe_dir_temp = self.get_value("buildozer", "recipe_dir")
        self.recipe_dir = Path(recipe_dir_temp) if recipe_dir_temp else None

        self._jars_dir = []
        jars_dir_temp = self.get_value("buildozer", "jars_dir")
        if jars_dir_temp and Path(jars_dir_temp).resolve().exists():
            self.jars_dir = Path(jars_dir_temp).resolve()

        android_arch = self.get_value("buildozer", "arch")
        if android_arch:
            self._arch = android_arch
        else:
            self.arch = self._find_arch()

        # maps to correct platform name incase the instruction set was specified
        self._arch = platform_map[self.arch]

        self._mode = self.get_value("buildozer", "mode")

        self.qt_libs_path: zipfile.Path = find_qtlibs_in_wheel(wheel_pyside=self.wheel_pyside)
        logging.info(f"[DEPLOY] Qt libs path inside wheel: {str(self.qt_libs_path)}")

        modls = self.get_value("qt", "modules")
        if modls:
            self._modules = modls.split(",")
        else:
            modls = self._find_pysidemodules()
            modls += self._find_qtquick_modules()
            # the order of the modules is the order in which the Qt libraries are loaded on
            # Android
            self.modules = self._order_qt_modules(modules=modls)

        # gets the xml dependency files from Qt installation path
        self.dependency_files = self._find_dependency_files()

        dependent_plugins = []
        self._local_libs = []
        loc_libs = self.get_value("buildozer", "local_libs")
        if loc_libs:
            self._local_libs = loc_libs.split(",")
        else:
            # the local_libs can also store dependent plugins
            local_libs, dependent_plugins = self._find_local_libs()
            self.local_libs = list(set(local_libs))

        self._qt_plugins = []
        qt_plgns = self.get_value("android", "plugins")
        if qt_plgns:
            self._qt_plugins = qt_plgns.split(",")
        elif dependent_plugins:
            self._find_plugin_dependencies(dependent_plugins)
            self.qt_plugins = list(set(dependent_plugins))

        recipe_dir_temp = self.get_value("buildozer", "recipe_dir")
        if recipe_dir_temp:
            self.recipe_dir = Path(recipe_dir_temp)

    @property
    def qt_plugins(self):
        return self._qt_plugins

    @qt_plugins.setter
    def qt_plugins(self, qt_plugins):
        self._qt_plugins = qt_plugins
        self.set_value("android", "plugins", ",".join(qt_plugins))

    @property
    def ndk_path(self):
        return self._ndk_path

    @ndk_path.setter
    def ndk_path(self, ndk_path: Path):
        self._ndk_path = ndk_path.resolve() if ndk_path else None
        if self._ndk_path:
            self.set_value("buildozer", "ndk_path", str(self._ndk_path))

    @property
    def sdk_path(self) -> Path:
        return self._sdk_path

    @sdk_path.setter
    def sdk_path(self, sdk_path: Path):
        self._sdk_path = sdk_path.resolve() if sdk_path else None
        if self._sdk_path:
            self.set_value("buildozer", "sdk_path", str(self._sdk_path))

    @property
    def arch(self):
        return self._arch

    @arch.setter
    def arch(self, arch):
        self._arch = arch
        self.set_value("buildozer", "arch", arch)

    @property
    def mode(self):
        return self._mode

    @property
    def modules(self):
        return self._modules

    @modules.setter
    def modules(self, modules):
        self._modules = modules
        self.set_value("qt", "modules", ",".join(modules))

    @property
    def local_libs(self):
        return self._local_libs

    @local_libs.setter
    def local_libs(self, local_libs):
        self._local_libs = local_libs
        self.set_value("buildozer", "local_libs", ",".join(local_libs))

    @property
    def recipe_dir(self):
        return self._recipe_dir

    @recipe_dir.setter
    def recipe_dir(self, recipe_dir: Path):
        self._recipe_dir = recipe_dir.resolve() if recipe_dir else None
        if self._recipe_dir:
            self.set_value("buildozer", "recipe_dir", str(self._recipe_dir))

    def recipes_exist(self):
        if not self._recipe_dir:
            return False

        pyside_recipe_dir = Path(self.recipe_dir) / "PySide6"
        shiboken_recipe_dir = Path(self.recipe_dir) / "shiboken6"

        return pyside_recipe_dir.is_dir() and shiboken_recipe_dir.is_dir()

    @property
    def jars_dir(self) -> Path:
        return self._jars_dir

    @jars_dir.setter
    def jars_dir(self, jars_dir: Path):
        self._jars_dir = jars_dir.resolve() if jars_dir else None
        if self._jars_dir:
            self.set_value("buildozer", "jars_dir", str(self._jars_dir))

    @property
    def wheel_pyside(self) -> Path:
        return self._wheel_pyside

    @wheel_pyside.setter
    def wheel_pyside(self, wheel_pyside: Path):
        self._wheel_pyside = wheel_pyside.resolve() if wheel_pyside else None
        if self._wheel_pyside:
            self.set_value("android", "wheel_pyside", str(self._wheel_pyside))

    @property
    def wheel_shiboken(self) -> Path:
        return self._wheel_shiboken

    @wheel_shiboken.setter
    def wheel_shiboken(self, wheel_shiboken: Path):
        self._wheel_shiboken = wheel_shiboken.resolve() if wheel_shiboken else None
        if self._wheel_shiboken:
            self.set_value("android", "wheel_shiboken", str(self._wheel_shiboken))

    @property
    def dependency_files(self):
        return self._dependency_files

    @dependency_files.setter
    def dependency_files(self, dependency_files):
        self._dependency_files = dependency_files

    def find_jars_dir(self):
        """Extract out and copy .jar files to {generated_files_path}
        """
        jars_dir = None
        if not self.dry_run:
            logging.info("[DEPLOY] Extract and copy jar files from PySide6 wheel to "
                         f"{self.generated_files_path}")
            jars_dir = extract_and_copy_jar(wheel_path=self.wheel_pyside,
                                            generated_files_path=self.generated_files_path)

        return jars_dir

    def _find_arch(self):
        """Find architecture from wheel name
        """
        arch = get_wheel_android_arch(wheel=self.wheel_pyside)
        if not arch:
            raise RuntimeError("[DEPLOY] PySide wheel corrupted. Wheel name should end with"
                               "platform name")

        return arch

    def _order_qt_modules(self, modules: list[str]) -> list[str]:
        """
        Given pysidedeploy_config.modules, add all the other dependent Qt modules and return
        them in loading order: every module comes after the modules it depends on. The Android
        QtLoader loads the Qt libraries in this order, and a Qt library crashes in its
        JNI_OnLoad if its dependencies are not loaded yet. The dependencies are found by using
        llvm-readobj (readelf) to find the dependent libraries from the module library.
        """
        pyside_only_modules = []
        ordered_libs = []
        lib_pattern = re.compile(f"libQt6(?P<mod_name>.*)_{self.arch}")

        llvm_readobj = get_llvm_readobj(self.ndk_path)
        if not llvm_readobj.exists():
            raise FileNotFoundError(f"[DEPLOY] {llvm_readobj} does not exist."
                                    "Finding Qt dependencies failed")

        archive = zipfile.ZipFile(self.wheel_pyside)
        lib_path_suffix = Path(str(self.qt_libs_path)).relative_to(self.wheel_pyside)

        with tempfile.TemporaryDirectory() as tmpdir:
            safe_extractall(archive, Path(tmpdir))
            qt_libs_tmpdir = Path(tmpdir) / lib_path_suffix
            # find the lib folder where Qt libraries are stored
            for module_name in sorted(set(modules)):
                if module_name in PYSIDE_ONLY_MODULES:
                    # No Qt library exists for these, so there is nothing to
                    # read Qt dependencies from.
                    logging.info(f"[DEPLOY] Skipping {module_name} while collecting Qt "
                                 "dependencies: it has no Qt library")
                    pyside_only_modules.append(module_name)
                    continue
                qt_module_path = qt_libs_tmpdir / f"libQt6{module_name}_{self.arch}.so"
                if not qt_module_path.exists():
                    raise FileNotFoundError(f"[DEPLOY] libQt6{module_name}_{self.arch}.so not found"
                                            " inside the wheel")
                find_lib_dependencies(llvm_readobj=llvm_readobj, lib_path=qt_module_path,
                                      dry_run=self.dry_run,
                                      used_dependencies=ordered_libs)

        # check if the PySide6 binary for the Qt module actually exists
        # eg: libQt6QmlModels.so exists and it includes QML types. Hence, it makes no
        # sense to load it as a module: it is loaded as a dependency of the libraries using it.
        pyside_modules = get_all_pyside_modules()
        ordered_modules = []
        for lib in ordered_libs:
            match = lib_pattern.search(lib)
            if match:
                module = match.group("mod_name")
                if module in modules or module in pyside_modules:
                    ordered_modules.append(module)

        dependent_modules_str = ",".join(module for module in ordered_modules
                                         if module not in modules)
        logging.info("[DEPLOY] The following extra dependencies were found:"
                     f" {dependent_modules_str}")

        return ordered_modules + pyside_only_modules

    def _find_dependency_files(self) -> list[zipfile.Path]:
        """
        Based on `modules`, returns the Qt6{module}_{arch}-android-dependencies.xml file, which
        contains the various dependencies of the module, like permissions, plugins etc
        """
        needed_dependency_files = [(f"Qt{MAJOR_VERSION}{module}_{self.arch}"
                                    "-android-dependencies.xml") for module in self.modules]
        found_dependency_files = []
        for dependency_file_name in needed_dependency_files:
            dependency_file = self.qt_libs_path / dependency_file_name
            if dependency_file.exists():
                found_dependency_files.append(dependency_file)

        logging.info("[DEPLOY] The following dependency files were found: "
                     f"{*found_dependency_files, }")

        return found_dependency_files

    def _find_local_libs(self):
        local_libs = set()
        plugins = set()
        lib_pattern = re.compile(f"lib(?P<lib_name>.*)_{self.arch}")
        for dependency_file in self._dependency_files:
            xml_content = dependency_file.read_text()
            root = ET.fromstring(xml_content)
            for local_lib in root.iter("lib"):

                if 'file' not in local_lib.attrib:
                    if 'name' not in local_lib.attrib:
                        logging.warning("[DEPLOY] Invalid android dependency file"
                                        f" {str(dependency_file)}")
                    continue

                file = local_lib.attrib['file']
                if file.endswith(".so"):
                    file = self._resolve_lib_name_on_disk(file)
                    # file_name starts with lib and ends with the platform name
                    # eg: lib<lib_name>_x86_64.so
                    file_name = Path(file).stem

                    # we only need lib_name, because lib and arch gets re-added by
                    # python-for-android
                    match = lib_pattern.search(file_name)
                    if match:
                        lib_name = match.group("lib_name")
                        local_libs.add(lib_name)
                        if lib_name.startswith("plugins"):
                            plugin_name = lib_name.split('plugins_', 1)[1]
                            plugins.add(plugin_name)

        return list(local_libs), list(plugins)

    def _resolve_lib_name_on_disk(self, file: str) -> str:
        """Return the lib path from the dependency xml, or the other name if only that exists.

        Qt lists some libs (eg: FFmpeg) with the ABI suffix, but ships them without it.
        androiddeployqt falls back to the name that exists in the same way.
        """
        qt_dir = self.qt_libs_path.parent
        if (qt_dir / file).exists():
            return file

        abi_suffix = f"_{self.arch}.so"
        if file.endswith(abi_suffix):
            alternative = file[:-len(abi_suffix)] + ".so"
        else:
            alternative = file[:-len(".so")] + abi_suffix

        if (qt_dir / alternative).exists():
            return alternative
        return file

    def _find_plugin_dependencies(self, dependent_plugins: list[str]):
        # The `bundled` element in the dependency xml files points to the folder where
        # additional dependencies for the application exists. Inspecting the depenency files
        # in android, this always points to the specific Qt plugin dependency folder.
        # eg: for application using Qt Multimedia, this looks like:
        # <bundled file="./plugins/multimedia" />
        # The code recusively checks all these dependent folders and adds the necessary plugins
        # as dependencies
        lib_pattern = re.compile(f"libplugins_(?P<plugin_name>.*)_{self.arch}.so")
        for dependency_file in self._dependency_files:
            xml_content = dependency_file.read_text()
            root = ET.fromstring(xml_content)
            for bundled_element in root.iter("bundled"):
                # the attribute 'file' can be misleading, but it always points to the plugin
                # folder on inspecting the dependency files
                if 'file' not in bundled_element.attrib:
                    logging.warning("[DEPLOY] Invalid Android dependency file"
                                    f" {str(dependency_file)}")
                    continue

                # from "./plugins/multimedia" to absolute path in wheel
                plugin_module_folder = bundled_element.attrib['file']
                # they all should start with `./plugins`
                if plugin_module_folder.startswith("./plugins"):
                    plugin_module_folder = plugin_module_folder.partition("./plugins/")[2]
                else:
                    continue

                absolute_plugin_module_folder = (self.qt_libs_path.parent / "plugins"
                                                 / plugin_module_folder)

                if not absolute_plugin_module_folder.is_dir():
                    logging.warning(f"[DEPLOY] Qt plugin folder '{plugin_module_folder}' does not"
                                    " exist or is not a directory for this Android platform")
                    continue

                for plugin in absolute_plugin_module_folder.iterdir():
                    plugin_name = plugin.name
                    if plugin_name.endswith(".so") and plugin_name.startswith("libplugins"):
                        # we only need part of plugin_name, because `lib` prefix and `arch` suffix
                        # gets re-added by python-for-android
                        match = lib_pattern.search(plugin_name)
                        if match:
                            plugin_infix_name = match.group("plugin_name")
                            if plugin_infix_name not in dependent_plugins:
                                dependent_plugins.append(plugin_infix_name)

    def find_recipe_dir(self):
        # create recipes
        # https://python-for-android.readthedocs.io/en/latest/recipes/
        # These recipes are manually added through buildozer.spec file to be used by
        # python_for_android while building the distribution

        recipe_dir = None
        if not self.recipes_exist() and not self.dry_run:
            logging.info("[DEPLOY] Creating p4a recipes for PySide6 and shiboken6")
            version = Wheel(self.wheel_pyside).version
            create_recipe(version=version, component=f"PySide{MAJOR_VERSION}",
                          wheel_path=self.wheel_pyside,
                          generated_files_path=self.generated_files_path,
                          qt_modules=self.modules,
                          local_libs=self.local_libs,
                          plugins=self.qt_plugins)
            create_recipe(version=version, component=f"shiboken{MAJOR_VERSION}",
                          wheel_path=self.wheel_shiboken,
                          generated_files_path=self.generated_files_path)
            recipe_dir = ((self.generated_files_path
                           / "recipes").resolve())
        return recipe_dir
