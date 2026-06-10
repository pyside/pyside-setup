# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations


import platform
import sys
from pathlib import Path
from email.generator import Generator

from .log import log
from .config import config
from .options import OPTION, CommandMixin
from .utils import is_64bit, copydir
from .wheel_utils import get_package_version, get_qt_version, macos_plat_name

wheel_module_exists = False


try:

    from packaging import tags
    from wheel import __version__ as wheel_version  # type: ignore[import-untyped]
    from setuptools.command.bdist_wheel import bdist_wheel as _bdist_wheel
    from setuptools.command.bdist_wheel import get_abi_tag, get_platform
    from setuptools.command.bdist_wheel import (  # type: ignore[attr-defined]
        safer_name as _safer_name
    )

    wheel_module_exists = True
except Exception as e:
    _bdist_wheel, wheel_version = type, ""  # type: ignore[misc,assignment]
    log.warning(f"***** Exception while trying to prepare bdist_wheel override class: {e}. "
                "Skipping wheel overriding.")


def get_bdist_wheel_override():
    return PysideBuildWheel if wheel_module_exists else None


class WheelSbomDistInfoMixin:
    """Inject a project's SBOM files into the wheel's '.dist-info/sboms' dir.

    Child classes need to implement ``sbom_source_dirs`` to point at the
    list of directory paths that contain SBOM files. A wheel may bundle more
    than one document (e.g. the split PySide6 essentials + addons documents).
    """

    def sbom_source_dirs(self) -> list[Path]:
        raise NotImplementedError

    def egg2dist(self, egg_info_path, dist_info_path):
        """
        Overrides the egg2dist method to copy SBOM files into the wheel
        '.dist-info/sboms' directory.

        This is the only point in time we can inject contents into the
        '.dist-info' directory, because before that, the parent egg2dist
        implementation removes any existing '.dist-info' directory.

        """
        # Call the original method, which creates the dist-info directory.
        super().egg2dist(egg_info_path, dist_info_path)

        # Copy the SBOM files, if this wheel bundles any SBOM documents at all.
        sbom_source_dirs = [Path(d) for d in self.sbom_source_dirs()]
        existing_sbom_source_dirs = [d for d in sbom_source_dirs if d.is_dir()]

        missing_dirs = [d for d in sbom_source_dirs if d not in existing_sbom_source_dirs]
        for missing_dir in missing_dirs:
            log.warning(f"SBOM source dir '{missing_dir}' not found; "
                        "skipping its inclusion into the wheel.")

        if not existing_sbom_source_dirs:
            log.info("No SBOM documents to bundle for this wheel; skipping.")
            return

        dist_info_sbom_dir = Path(dist_info_path) / "sboms"

        # The documents are expected to have distinct file names, so their
        # contents can be merged into the single '.dist-info/sboms' directory.
        for sbom_source_dir in existing_sbom_source_dirs:
            _vars = {
                "sbom_source_dir": sbom_source_dir,
                "dist_info_sbom_dir": dist_info_sbom_dir,
            }
            log.info(f"Copying SBOM files from '{sbom_source_dir}'.")
            copydir("{sbom_source_dir}", "{dist_info_sbom_dir}", _vars=_vars)


class PysideBuildWheel(WheelSbomDistInfoMixin, _bdist_wheel, CommandMixin):

    user_options = (  # type: ignore[assignment]
        _bdist_wheel.user_options + CommandMixin.mixin_user_options
        if wheel_module_exists else None)

    def __init__(self, *args, **kwargs):
        self.command_name = "bdist_wheel"
        self._package_version = None
        _bdist_wheel.__init__(self, *args, **kwargs)
        CommandMixin.__init__(self)

    def finalize_options(self):
        CommandMixin.mixin_finalize_options(self)
        if sys.platform == 'darwin' and not self.is_cross_compile:
            # Override the platform name to contain the correct
            # minimum deployment target.
            # This is used in the final wheel name.
            self.plat_name = macos_plat_name()

        # When limited API is requested, notify bdist_wheel to
        # create a properly named package, which will contain
        # the initial cpython version we support. This must match the
        # Py_LIMITED_API level the sources are compiled against, which is
        # 0x030a0000, and PySide6's own requires-python floor.
        limited_api_enabled = OPTION["LIMITED_API"] == 'yes'
        if limited_api_enabled:
            self.py_limited_api = "cp310"

        self._package_version = get_package_version()

        _bdist_wheel.finalize_options(self)

    @property
    def wheel_dist_name(self):
        # Slightly modified version of wheel's wheel_dist_name
        # method, to add the Qt version as well.
        # Example:
        #   PySide6-6.3-6.3.2-cp36-abi3-macosx_10_10_intel.whl
        # The PySide6 version is "6.3".
        # The Qt version built against is "6.3.2".
        wheel_version = f"{self._package_version}-{get_qt_version()}"
        components = (_safer_name(self.distribution.get_name()), wheel_version)
        if self.build_number:
            components += (self.build_number,)  # type: ignore[assignment]
        return '-'.join(components)

    def sbom_source_dirs(self) -> list[Path]:
        # Read this project's SBOM documents from the CMake install prefix.
        build_command = self.get_finalized_command('build')
        sbom_root = Path(build_command.install_dir) / "share" / "sbom"

        # The PySide6 build has multiple sbom source dirs.
        if config.is_internal_pyside_build():
            sbom_dirs = [sbom_root / d for d in ("PySide6-Essentials", "PySide6-Addons",
                                                 "PySide6-WebEngine", "PySide6-Pdf",
                                                 "PySide6-Tools-Essentials",
                                                 "PySide6-Tools-Addons")]
            return [d for d in sbom_dirs if d.is_dir()]

        # shiboken6 / shiboken6_generator: a single document named after the
        # package.
        return [sbom_root / config.package_name()]

    # Modify the returned wheel tag tuple to use correct python version
    # info when cross-compiling. We use the python info extracted from
    # the shiboken python config test.
    # setuptools / wheel don't support cross compiling out of the box
    # at the moment. Relevant discussion at
    # https://discuss.python.org/t/towards-standardizing-cross-compiling/10357
    def get_cross_compiling_tag_tuple(self, tag_tuple):
        (old_impl, old_abi_tag, plat_name) = tag_tuple

        # Compute tag from the python version that the build command
        # queried.
        build_command = self.get_finalized_command('build')
        python_target_info = (
            build_command.python_target_info['python_info']  # type: ignore[attr-defined]
        )

        impl = 'no-py-ver-impl-available'
        abi = 'no-abi-tag-info-available'
        py_version = python_target_info['version'].split('.')
        py_version_major, py_version_minor, _ = py_version

        so_abi = python_target_info['so_abi']
        if so_abi and so_abi.startswith('cpython-'):
            interpreter_name, cp_version = so_abi.split('-')[:2]
            impl_name = tags.INTERPRETER_SHORT_NAMES.get(interpreter_name) or interpreter_name
            impl_ver = f"{py_version_major}{py_version_minor}"
            impl = impl_name + impl_ver
            abi = f'cp{cp_version}'
        tag_tuple = (impl, abi, plat_name)
        return tag_tuple

    # Adjust wheel tag for limited api and cross compilation.
    @staticmethod
    def adjust_cross_compiled_many_linux_tag(old_tag):
        (old_impl, old_abi_tag, old_plat_name) = old_tag

        new_plat_name = old_plat_name

        # TODO: Detect glibc version instead. We're abusing the
        # manylinux2014 tag here, just like we did with manylinux1
        # for x86_64 builds.
        many_linux_prefix = 'manylinux2014'
        linux_prefix = "linux_"
        if old_plat_name.startswith(linux_prefix):
            # Extract the arch suffix like -armv7l or -aarch64
            _index = old_plat_name.index(linux_prefix) + len(linux_prefix)
            plat_name_arch_suffix = old_plat_name[_index:]

            new_plat_name = f"{many_linux_prefix}_{plat_name_arch_suffix}"

        tag = (old_impl, old_abi_tag, new_plat_name)
        return tag

    # Adjust wheel tag for limited api and cross compilation.
    def adjust_tag_and_supported_tags(self, old_tag, supported_tags):
        tag = old_tag
        (old_impl, old_abi_tag, old_plat_name) = old_tag

        # Get new tag for cross builds.
        if self.is_cross_compile:
            tag = self.get_cross_compiling_tag_tuple(old_tag)

        # Use PEP600 for manylinux wheel name
        # For Qt6 we know RHEL 8.4 is the base linux platform,
        # and has GLIBC 2.28.
        # This will generate a name that contains:
        #     manylinux_2_28
        # TODO: Add actual distro detection, instead of
        # relying on limited_api option.
        if (old_plat_name in ('linux-x86_64', 'linux_x86_64')
                and is_64bit()
                and self.py_limited_api):
            _, _version = platform.libc_ver()
            glibc = _version.replace(".", "_")
            tag = (old_impl, old_abi_tag, f"manylinux_{glibc}_x86_64")

        # Set manylinux tag for cross-compiled builds when targeting
        # limited api.
        if self.is_cross_compile and self.py_limited_api:
            tag = self.adjust_cross_compiled_many_linux_tag(tag)

        # Reset the abi name and python versions supported by this wheel
        # when targeting limited API. This is the same code that's
        # in get_tag(), but done later after our own customizations.
        if self.py_limited_api and old_impl.startswith('cp3'):
            (_, _, adjusted_plat_name) = tag
            impl = self.py_limited_api
            abi_tag = 'abi3'
            tag = (impl, abi_tag, adjusted_plat_name)

        # If building for limited API or we created a new tag, add it
        # to the list of supported tags.
        if tag != old_tag or self.py_limited_api:
            supported_tags.append(tag)
        return tag

    # A slightly modified copy of get_tag from bdist_wheel.py, to allow
    # adjusting the returned tag without triggering an assert. Otherwise
    # we would have to rename wheels manually.
    # Copy is up-to-date since commit
    # 0acd203cd896afec7f715aa2ff5980a403459a3b in the wheel repo.
    def get_tag(self):
        # bdist sets self.plat_name if unset, we should only use it for purepy
        # wheels if the user supplied it.
        if self.plat_name_supplied:
            plat_name = self.plat_name or ""
        elif self.root_is_pure:
            plat_name = 'any'
        else:
            # macosx contains system version in platform name so need special handle
            if self.plat_name and not self.plat_name.startswith("macosx"):
                plat_name = self.plat_name
            else:
                # on macOS always limit the platform name to comply with any
                # c-extension modules in bdist_dir, since the user can specify
                # a higher MACOSX_DEPLOYMENT_TARGET via tools like CMake

                # on other platforms, and on macOS if there are no c-extension
                # modules, use the default platform name.
                plat_name = get_platform(self.bdist_dir)

            if plat_name in ('linux-x86_64', 'linux_x86_64') and not is_64bit():
                plat_name = 'linux_i686'

        plat_name = plat_name.lower().replace('-', '_').replace('.', '_')

        if self.root_is_pure:
            if self.universal:
                impl = 'py3'
            else:
                impl = self.python_tag
            tag = (impl, 'none', plat_name)
        else:
            impl_name = tags.interpreter_name()
            impl_ver = tags.interpreter_version()
            impl = impl_name + impl_ver
            # We don't work on CPython 3.1, 3.0.
            if self.py_limited_api and (impl_name + impl_ver).startswith('cp3'):
                impl = self.py_limited_api
                abi_tag = 'abi3'
            else:
                abi_tag = str(get_abi_tag()).lower()
            tag = (impl, abi_tag, plat_name)
            # issue gh-374: allow overriding plat_name
            supported_tags = [(t.interpreter, t.abi, plat_name)
                              for t in tags.sys_tags()]
            # PySide's custom override.
            tag = self.adjust_tag_and_supported_tags(tag, supported_tags)
            assert tag in supported_tags, (f"would build wheel with unsupported tag {tag}")
        return tag

    # Copy of get_tag from bdist_wheel.py, to write a triplet Tag
    # only once for the limited_api case.
    def write_wheelfile(self, wheelfile_base, generator=f'bdist_wheel ({wheel_version})'):
        from email.message import Message
        msg = Message()
        msg['Wheel-Version'] = '1.0'  # of the spec
        msg['Generator'] = generator
        msg['Root-Is-Purelib'] = str(self.root_is_pure).lower()
        if self.build_number is not None:
            msg['Build'] = self.build_number

        # Doesn't work for bdist_wininst
        impl_tag, abi_tag, plat_tag = self.get_tag()
        # To enable pypi upload we are adjusting the wheel name
        pypi_ready = True if OPTION["LIMITED_API"] else False

        def writeTag(impl):
            for abi in abi_tag.split('.'):
                for plat in plat_tag.split('.'):
                    msg['Tag'] = '-'.join((impl, abi, plat))
        if pypi_ready:
            writeTag(impl_tag)
        else:
            for impl in impl_tag.split('.'):
                writeTag(impl)

        wheelfile_path = Path(wheelfile_base) / 'WHEEL'
        log.info(f'creating {wheelfile_path}')
        with open(wheelfile_path, 'w') as f:
            Generator(f, maxheaderlen=0).flatten(msg)


class CreateWheelsBuildWheel(WheelSbomDistInfoMixin, _bdist_wheel):
    """Minimal bdist_wheel override used by create_wheels.py.

    The SBOM directories are provided via the ``sbom_dirs`` class attribute
    (a list of directory paths), injected into the setup.py.base template.
    """

    sbom_dirs: list[Path | str] = []

    def sbom_source_dirs(self) -> list[Path]:
        return [Path(d) for d in self.sbom_dirs]


if not wheel_module_exists:
    del PysideBuildWheel
    del CreateWheelsBuildWheel
