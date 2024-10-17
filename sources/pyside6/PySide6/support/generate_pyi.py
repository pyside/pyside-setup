# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

"""
generate_pyi.py

This script generates the .pyi files for all PySide modules.
"""
# mypy: disable-error-code="import-not-found"

import argparse
import inspect  # noqa: F401
import logging
import os
import sys
import typing  # noqa: F401

from pathlib import Path
from types import SimpleNamespace  # noqa: F401

# Can we use forward references?
USE_PEP563 = sys.version_info[:2] >= (3, 7)


def generate_all_pyi(outpath, options):
    ps = os.pathsep
    if options.sys_path:
        # make sure to propagate the paths from sys_path to subprocesses
        normpath = lambda x: os.fspath(Path(x).resolve())  # noqa: E731
        sys_path = [normpath(_) for _ in options.sys_path]
        sys.path[0:0] = sys_path
        pypath = ps.join(sys_path)
        os.environ["PYTHONPATH"] = pypath

    # now we can import
    global PySide6, inspect, typing, HintingEnumerator, build_brace_pattern
    import PySide6
    from PySide6.support.signature.lib.enum_sig import HintingEnumerator
    from PySide6.support.signature.lib.tool import build_brace_pattern
    from PySide6.support.signature.lib.pyi_generator import generate_pyi

    # propagate USE_PEP563 to the mapping module.
    # Perhaps this can be automated?
    PySide6.support.signature.mapping.USE_PEP563 = USE_PEP563

    outpath = Path(outpath) if outpath and os.fspath(outpath) else Path(PySide6.__file__).parent
    name_list = PySide6.__all__ if options.modules == ["all"] else options.modules
    errors = ", ".join(set(name_list) - set(PySide6.__all__))
    if errors:
        raise ImportError(f"The module(s) '{errors}' do not exist")
    for mod_name in name_list:
        import_name = "PySide6." + mod_name
        if hasattr(sys, "pypy_version_info"):
            # PYSIDE-535: We cannot use __feature__ yet in PyPy
            generate_pyi(import_name, outpath, options)
        else:
            from PySide6.support import feature
            feature_id = feature.get_select_id(options.feature)
            with feature.force_selection(feature_id, import_name):
                generate_pyi(import_name, outpath, options)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="This script generates the .pyi file for all PySide modules.")
    parser.add_argument("modules", nargs="+",
                        help="'all' or the names of modules to build (QtCore QtGui etc.)")
    parser.add_argument("--quiet", action="store_true", help="Run quietly")
    parser.add_argument("--outpath",
                        help="the output directory (default = binary location)")
    parser.add_argument("--sys-path", nargs="+",
                        help="a list of strings prepended to sys.path")
    parser.add_argument("--feature", nargs="+", choices=["snake_case", "true_property"], default=[],
                        help="""a list of feature names. """
                        """Example: `--feature snake_case true_property`. """
                        """Currently not available for PyPy.""")
    options = parser.parse_args()

    qtest_env = os.environ.get("QTEST_ENVIRONMENT", "")
    log_level = logging.DEBUG if qtest_env else logging.INFO
    if options.quiet:
        log_level = logging.WARNING
    logging.basicConfig(level=log_level)
    logger = logging.getLogger("generate_pyi")

    outpath = options.outpath
    if outpath and not Path(outpath).exists():
        os.makedirs(outpath)
        logger.info(f"+++ Created path {outpath}")
    options._pyside_call = True
    options.logger = logger
    options.is_ci = qtest_env == "ci"
    generate_all_pyi(outpath, options=options)
# eof
