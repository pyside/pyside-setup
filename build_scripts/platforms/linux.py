# Copyright (C) 2018 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

from pathlib import Path

from ..log import log
from ..config import config
from ..options import OPTION
from ..utils import (copy_icu_libs, copydir, copyfile, find_files_using_glob,
                     linux_patch_executable, RpathTargetType)
from .. import PYSIDE, PYSIDE_UNIX_BUNDLED_TOOLS


def prepare_standalone_package_linux(pyside_build, _vars, cross_build=False, is_android=False):
    built_modules = _vars['built_modules']

    constrain_modules = None
    copy_plugins = True
    copy_qml = True
    copy_translations = True
    copy_qt_conf = True

    log.info("Copying files...")

    if config.is_internal_shiboken_generator_build():
        constrain_modules = ["Core", "Network", "Xml", "XmlPatterns"]
        copy_plugins = False
        copy_qml = False
        copy_translations = False
        copy_qt_conf = False

    # <qt>/lib/* -> <setup>/{st_package_name}/Qt/lib
    destination_dir = Path("{st_build_dir}/{st_package_name}".format(**_vars))
    destination_qt_dir = destination_dir / "Qt"
    destination_qt_lib_dir = destination_qt_dir / "lib"

    # android libs does not have the Qt major version
    if is_android:
        lib_regex = 'libQt6*.so*'
    else:
        lib_regex = 'libQt6*.so.?'

    accepted_modules = [lib_regex]
    if constrain_modules:
        accepted_modules = [f"libQt6{module}*.so.?" if not is_android else f"libQt6{module}*.so*"
                            for module in constrain_modules]
    accepted_modules.append("libicu*.so.??")

    if is_android:
        accepted_modules.append("*-android-dependencies.xml")

    copydir("{qt_lib_dir}", destination_qt_lib_dir,
            _filter=accepted_modules,
            recursive=False, _vars=_vars, force_copy_symlinks=True)

    if not cross_build and not is_android:
        # Check if ICU libraries were copied over to the destination
        # Qt libdir.
        maybe_icu_libs = find_files_using_glob(destination_qt_lib_dir, "libicu*")

        # If no ICU libraries are present in the Qt libdir (like when
        # Qt is built against system ICU, or in the Coin CI where ICU
        # libs are in a different directory) try to find out / resolve
        # which ICU libs are used by QtCore (if used at all) using a
        # custom written ldd (non-cross build only), and copy the ICU
        # libs to the Pyside Qt dir if necessary.
        # We choose the QtCore lib to inspect, by
        # checking which QtCore library the shiboken6 executable uses.
        if not maybe_icu_libs:
            copy_icu_libs(pyside_build._patchelf_path, destination_qt_lib_dir)

    # Set RPATH for Qt libs.
    if not is_android:
        pyside_build.update_rpath_for_linux(RpathTargetType.qt_libraries,
                                            qt_lib_dir=destination_qt_lib_dir)

    # Patching designer to use the Qt libraries provided in the wheel
    if config.is_internal_pyside_build() and not OPTION['NO_QT_TOOLS']:

        for tool in PYSIDE_UNIX_BUNDLED_TOOLS:
            tool_path = destination_dir / tool
            linux_patch_executable(pyside_build._patchelf_path, tool_path)

    if pyside_build.is_webengine_built(built_modules):
        copydir("{qt_data_dir}/resources",
                destination_qt_dir / "resources",
                _filter=None,
                recursive=False,
                _vars=_vars)

    if copy_plugins:
        is_pypy = "pypy" in pyside_build.build_classifiers
        # <qt>/plugins/* -> <setup>/{st_package_name}/Qt/plugins
        plugins_target = destination_qt_dir / "plugins"
        copydir("{qt_plugins_dir}", plugins_target,
                _filter=["*.so"],
                recursive=True,
                _vars=_vars)
        if not is_pypy and not is_android:
            copydir("{install_dir}/plugins/designer",
                    plugins_target / "designer",
                    _filter=["*.so"],
                    recursive=False,
                    _vars=_vars)

        copied_plugins = pyside_build.get_shared_libraries_in_path_recursively(
            plugins_target)
        if not is_android:
            pyside_build.update_rpath_for_linux(RpathTargetType.plugins, paths=copied_plugins)

    if copy_qml:
        qml_ignore = ["*.debug", "*.la", "*.a", "*.o", "*.cmake", "*.pc", "*.prl"]

        # <qt>/qml/* -> <setup>/{st_package_name}/Qt/qml
        qml_plugins_target = destination_qt_dir / "qml"
        copydir("{qt_qml_dir}",
                qml_plugins_target,
                _filter=None,
                force=False,
                recursive=True,
                ignore=qml_ignore,
                _vars=_vars)
        copied_plugins = pyside_build.get_shared_libraries_in_path_recursively(
            qml_plugins_target)
        if not is_android:
            pyside_build.update_rpath_for_linux(
                RpathTargetType.qml_plugins,
                paths=copied_plugins,
                qt_lib_dir=destination_qt_lib_dir)

    if copy_translations:
        # <qt>/translations/* ->
        # <setup>/{st_package_name}/Qt/translations
        copydir("{qt_translations_dir}",
                destination_qt_dir / "translations",
                _filter=["*.qm", "*.pak"],
                force=False,
                _vars=_vars)

    if copy_qt_conf:
        # Copy the qt.conf file to libexec.
        qt_libexec_path = destination_qt_dir / "libexec"
        if not qt_libexec_path.is_dir():
            qt_libexec_path.mkdir(parents=True)
        copyfile(f"{{build_dir}}/{PYSIDE}/{{st_package_name}}/qt.conf",
                 qt_libexec_path, _vars=_vars)
