# Copyright (C) 2018 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import fnmatch
from pathlib import Path

from ..log import log
from ..config import config
from ..options import OPTION
from ..utils import (copydir, copyfile, macos_add_rpath,
                     macos_fix_rpaths_for_library)
from .. import PYSIDE, PYSIDE_UNIX_BUNDLED_TOOLS


def _macos_patch_executable(name, _vars=None):
    """ Patch an executable to run with the Qt libraries. """
    upper_name = name.capitalize()
    bundle = f"{{st_build_dir}}/{{st_package_name}}/{upper_name}.app".format(**_vars)
    binary = f"{bundle}/Contents/MacOS/{upper_name}"
    rpath = "@loader_path/../../../Qt/lib"
    macos_add_rpath(rpath, binary)


def prepare_standalone_package_macos(pyside_build, _vars, is_android=False, is_ios=False):
    built_modules = _vars['built_modules']

    constrain_modules = None
    copy_plugins = True
    copy_qml = True
    copy_translations = True
    copy_qt_conf = True

    destination_dir = Path("{st_build_dir}/{st_package_name}".format(**_vars))
    destination_qt_dir = destination_dir / "Qt"
    destination_qt_lib_dir = destination_qt_dir / "lib"
    log.info("Copying files...")

    if config.is_internal_shiboken_generator_build():
        constrain_modules = ["Core", "Network", "Xml", "XmlPatterns"]
        constrain_frameworks = [f"Qt{name}.framework" for name in constrain_modules]
        copy_plugins = False
        copy_qml = False
        copy_translations = False
        copy_qt_conf = False

    # Directory filter for skipping unnecessary files.
    def general_dir_filter(dir_name, parent_full_path, dir_full_path):
        if fnmatch.fnmatch(dir_name, "*.dSYM"):
            return False
        if is_ios and dir_name == 'objects-Debug':
            return False
        return True

    # Filter out debug plugins and qml plugins in the
    # debug_and_release config.
    no_copy_debug = True

    def file_variant_filter(file_name, file_full_path):
        if is_ios:
            return not file_name.endswith(('_debug.a', '_debug.prl'))
        if pyside_build.qtinfo.build_type != 'debug_and_release':
            return True
        if file_name.endswith('_debug.dylib') and no_copy_debug:
            return False
        return True

    # Patching designer to use the Qt libraries provided in the wheel
    if config.is_internal_pyside_build() and not OPTION['NO_QT_TOOLS'] and not is_ios:
        for tool in PYSIDE_UNIX_BUNDLED_TOOLS:
            _macos_patch_executable(tool, _vars)

    # <qt>/lib/* -> <setup>/{st_package_name}/Qt/lib
    if pyside_build.qt_is_framework_build():
        def framework_dir_filter(dir_name, parent_full_path, dir_full_path):
            if '.framework' in dir_name:
                if (dir_name.startswith('QtWebEngine')
                        and not pyside_build.is_webengine_built(built_modules)):
                    return False
                if constrain_modules and dir_name not in constrain_frameworks:
                    return False

            if dir_name == 'fonts':
                return False
            if dir_name == 'Headers' and not is_ios:
                return False
            if str(dir_full_path).endswith('Versions/Current'):
                return False
            if str(dir_full_path).endswith('Versions/5/Resources'):
                return False
            if str(dir_full_path).endswith('Versions/5/Helpers'):
                return False
            return general_dir_filter(dir_name, parent_full_path, dir_full_path)

        # Filter out debug frameworks in the
        # debug_and_release config.
        no_copy_debug = True

        def framework_variant_filter(file_name, file_full_path):
            if is_ios:
                return not file_name.endswith(('_debug', '_debug.a', '_debug.prl', '-debug.cmake'))
            if pyside_build.qtinfo.build_type != 'debug_and_release':
                return True
            dir_path = Path(file_full_path).parent
            in_framework = str(dir_path).endswith("Versions/5")
            if file_name.endswith('_debug') and in_framework and no_copy_debug:
                return False
            return True

        lib_ignore = ["*.la", "*.pc"] if is_ios else ["*.la", "*.a", "*.cmake", "*.pc", "*.prl"]

        copydir("{qt_lib_dir}", destination_qt_lib_dir,
                recursive=True, _vars=_vars,
                ignore=lib_ignore,
                dir_filter_function=framework_dir_filter,
                file_filter_function=framework_variant_filter)

        # Fix rpath for WebEngine process executable. The already
        # present rpath does not work because it assumes a symlink
        # from Versions/5/Helpers, thus adding two more levels of
        # directory hierarchy.
        if pyside_build.is_webengine_built(built_modules):
            bundle = Path("QtWebEngineCore.framework/Helpers/") / "QtWebEngineProcess.app"
            binary = "Contents/MacOS/QtWebEngineProcess"
            webengine_process_path = bundle / binary
            final_path = destination_qt_lib_dir / webengine_process_path
            rpath = "@loader_path/../../../../../"
            macos_fix_rpaths_for_library(final_path, rpath)
    else:
        ignored_modules = []
        if not pyside_build.is_webengine_built(built_modules):
            ignored_modules.extend(['libQt6WebEngine*.dylib'])

        accepted_modules = ['libQt6*.6.dylib']
        if is_android:
            accepted_modules = ['libQt6*.so', '*-android-dependencies.xml']
        elif is_ios:
            accepted_modules = ['libQt6*.a']

        if constrain_modules:
            accepted_modules = [f"libQt6{module}*.6.dylib" for module in constrain_modules]

        copydir("{qt_lib_dir}", destination_qt_lib_dir,
                _filter=accepted_modules,
                ignore=ignored_modules,
                file_filter_function=file_variant_filter,
                recursive=True, _vars=_vars, force_copy_symlinks=True)

        if pyside_build.is_webengine_built(built_modules):
            copydir("{qt_data_dir}/resources",
                    destination_qt_dir / "resources",
                    _filter=None,
                    recursive=False,
                    _vars=_vars)

            # Fix rpath for WebEngine process executable.
            qt_libexec_path = Path(destination_qt_dir) / "libexec"
            binary = "QtWebEngineProcess"
            final_path = qt_libexec_path / binary
            rpath = "@loader_path/../lib"
            macos_fix_rpaths_for_library(final_path, rpath)

            if copy_qt_conf:
                # Copy the qt.conf file to libexec.
                if not qt_libexec_path.is_dir():
                    qt_libexec_path.mkdir(parents=True)
                copyfile(
                    f"{{build_dir}}/{PYSIDE}/{{st_package_name}}/qt.conf",
                    qt_libexec_path, _vars=_vars)

    if copy_plugins:
        is_pypy = "pypy" in pyside_build.build_classifiers
        # <qt>/plugins/* -> <setup>/{st_package_name}/Qt/plugins
        plugins_target = destination_qt_dir / "plugins"
        filters = ["*.dylib"]
        if is_android:
            filters = ["*.so"]
        elif is_ios:
            filters = ["*.a", "*.prl"]
        copydir("{qt_plugins_dir}", plugins_target,
                _filter=filters,
                recursive=True,
                dir_filter_function=general_dir_filter,
                file_filter_function=file_variant_filter,
                _vars=_vars)
        if not is_pypy:
            copydir("{install_dir}/plugins/designer",
                    plugins_target / "designer",
                    _filter=filters,
                    recursive=False,
                    force=False,
                    _vars=_vars)

    if copy_qml:
        # Some QML plugins (eg. Qt.labs.assetdownloader) ship a static archive
        # alongside their dylib as a CMake build artifact. Nuitka mishandles .a
        # files it encounters while walking dependencies, so keep them out of
        # the wheel just like the Qt/lib and Qt/plugins copies already do.
        qml_ignore = (["*.la", "*.pc"] if is_ios
                      else ["*.la", "*.a", "*.o", "*.cmake", "*.pc", "*.prl"])

        # <qt>/qml/* -> <setup>/{st_package_name}/Qt/qml
        copydir("{qt_qml_dir}", destination_qt_dir / "qml",
                _filter=None,
                ignore=qml_ignore,
                recursive=True,
                force=False,
                dir_filter_function=general_dir_filter,
                file_filter_function=file_variant_filter,
                _vars=_vars)

    if copy_translations:
        # <qt>/translations/* ->
        # <setup>/{st_package_name}/Qt/translations
        copydir("{qt_translations_dir}", destination_qt_dir / "translations",
                _filter=["*.qm", "*.pak"],
                force=False,
                _vars=_vars)
