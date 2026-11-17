.. _buildingfromsource:

Building from Source
====================

.. toctree::
    :maxdepth: 1
    :hidden:

    linux.rst
    macOS.rst
    windows.rst

.. important:: This page is focused on building |project| **from source**.
  If you just want to install |pymodname|, you need to run: :command:`pip install pyside6`.

  For more details, refer to our :ref:`getting-started` guide. Additionally, you can check the
  :ref:`FAQ <faq>` related to the project.

.. _Quick Start: gettingstarted.html

General Requirements
--------------------

The following prerequisites must be installed before you build |project|.
On **Linux** you might get them with your operating system package manager, on **macOS**
you might get them with ``brew``, and on **Windows** you can download the installer from each
website.

* **Python**: 3.13.5+ (`official Python website`_) for building wheels that
  support Python 3.10-3.15 using the `Stable API`_, which requires `GH 134989`_.
  When targeting only one Python minor version not using the `Stable API`_,
  any version from 3.10-3.15 may be used.
* **Qt:** 6.12+ `online installer`_
* **CMake:** 3.25+ `official CMake website`_
* **Git:** 2.0+. `official Git website`_
* **libclang:** The libclang library (supported versions: 16-23, recommended version: 22.1.8+ for 6.12+).
  Prebuilt versions for each OS can be `downloaded here`_.
* Check the `Supported Platforms of Qt`_

Guides per platform
-------------------

You can refer to the following pages for platform specific instructions:

.. grid:: 1 3 3 3
    :gutter: 2

    .. grid-item-card::
        :img-top: ../images/windows.svg
        :class-item: text-center

        +++
        .. button-ref:: windows
            :color: primary
            :outline:
            :expand:

            Windows

    .. grid-item-card::
        :img-top: ../images/macos.svg
        :class-item: text-center

        +++
        .. button-ref:: macOS
            :color: primary
            :outline:
            :expand:

            macOS

    .. grid-item-card::
        :img-top: ../images/linux.svg
        :class-item: text-center

        +++
        .. button-ref:: linux
            :color: primary
            :outline:
            :expand:

            Linux

.. important:: |project| does not yet support WebAssembly and the mobile operating systems (Android or iOS).
               Most Linux-based embedded OS provide PySide with their official
               package manager (for example, `Raspbian`_ and `ArchlinuxARM`_).

.. _Raspbian: https://www.raspbian.org/
.. _ArchlinuxARM: https://archlinuxarm.org/

A normal building command will look like this::

    python setup.py install --qtpaths=/path/to/qtpaths \
                            --ignore-git \
                            --debug \
                            --build-tests \
                            --parallel=8 \
                            --verbose-build \
                            --module-subset=Core,Gui,Widgets

Which will build and install the project with **debug** symbols, including the **tests**,
using **ninja** (instead of make), and considering only the **module subset** of
:mod:`QtCore <PySide6.QtCore>`, :mod:`QtGui <PySide6.QtGui>`, and
:mod:`QtWidgets <PySide6.QtWidgets>`.

`CMake Unity Build Mode`_ is used by default for speed-up.

Other important options to consider are:
 * ``--cmake``, to specify the path to the cmake binary,
 * ``--reuse-build``, to rebuild only the modified files,
 * ``--openssl=/path/to/openssl/bin``, to use a different path for OpenSSL,
 * ``--standalone``, to copy over the Qt libraries into the final package to make it work on other
   machines,
 * ``--build-docs``, to enable the documentation build.
 * ``--doc-build-online``, to build documentation using the online template (by
   default it uses the offline)
 * ``--sbom``, to generate Software Bill of Materials (SBOM) documents, see
   :ref:`generating_sboms`.

Testing the installation
------------------------

Once the installation finishes, you will be able to execute any of our examples::

  python examples/widgets/widgets/tetrix/tetrix.py

Running Tests
-------------

Using the ``--build-tests`` option will enable us to run all the auto tests inside the project::

  python testrunner.py test > testlog.txt

.. note:: On Windows, don't forget to have qtpaths in your path
   (:command:`set PATH=C:\\\Path\\\to\\\Qt\\\6.12.0\\\msvc2022_64\\\bin;%PATH%`)

You can also run a specific test (for example ``qpainter_test``) by running::

    ctest -R qpainter_test --verbose

.. _cross_compilation:

Cross Compilation
-----------------

Starting from 6.3, it is possible to cross-compile Shiboken (module), and
PySide. This functionality is still in Technical Preview, which means it could
change in the future releases.

.. important:: The only supported configuration is using a host Linux
   machine to cross-compile to a Linux target platform.

Cross compiling software is a valid use case that many projects rely on,
however, it is a complicated process that might fail due to many reasons.

Before starting with the process, it is important to understand the details of
the build system, and the goal of cross compilation.

In the build process, a ``Host`` is the computer you are currently using to
compile, and a ``Target`` is your embedded device that you are compiling for.

Qt for Python is being built using setuptools, and relies on a ``setup.py`` file
that is called recursively to build Shiboken (module),
Shiboken (generator), and PySide. As the generator is creating
the wrappers for the bindings, it's not cross compiled
for the target.
Only the Shiboken (module) and PySide are cross compiled.

The building process requires a Qt installation, and a Python interpreter
on both the host, and the target. The used Qt versions on both platforms
should have the same minor version. That is, Qt 6.3 (host)
cannot be used with a Qt 6.2 (target), or the other way around.

.. note:: It is recommended to use the same version of Qt on both host and target to avoid possible
  unconventional errors.

Prerequisites
~~~~~~~~~~~~~

First and foremost, you need to have access to the target device because you
need to copy several system files (sysroot). We recommend a Linux OS that has
the latest Qt versions, like `Manjaro ARM`_ or `Archlinux ARM`_.

* (target) Install Qt 6.12+ on the system using the package manager.
* (host) Install Qt 6.12+ on the system using the package manager or Qt
  Installer.
* (target, host) Install the library and development packages that provide
  C++ headers, linkers, libraries, and compilers.
* (target, host) Install Python interpreter v3.10 or later
* (target, host) Install CMake 3.22+

After installing these prerequisites, copy the ``target`` sysroot to your
``host`` computer. This process is tricky, because copying system files from
another computer might cause problems with the symbolic links. Here you
have two options to achieve that.

Option A: Copying the files
***************************

Create a directory to copy the sysroot of your target device,
for example ``rpi-sysroot``, and perform the copy on your host computer:

.. code-block:: bash

    rsync -vR --progress -rl --delete-after --safe-links \
        USERNAME@TARGET_IP:/{lib,usr,opt/vc/lib} rpi-sysroot/

Ensure to replace ``USERNAME`` and ``TARGET_IP`` with your system appropriate
values.

Option B: Packaging the file system
***********************************

Create a package for your sysroot in your target:

.. code-block:: bash

    tar cfJ ~/sysroot.tar.xz /lib /usr /opt/vc/lib

Copy the package from the target to your host:

.. code-block:: bash

    rsync -vR --progress USERNAME@TARGET_IP:sysroot.tar.xz .

Once you have the tar file, unpack it inside a ``rpi-sysroot`` directory.

Fix Symlinks
************

It is recommended to run the following script to fix
most of the issues you would find with symbolic links:

.. code-block:: python

    import sys
    from pathlib import Path
    import os

    # Take a sysroot directory and turn all the absolute symlinks and turn them into
    # relative ones such that the sysroot is usable within another system.

    if len(sys.argv) != 2:
        print(f"Usage is {sys.argv[0]} <sysroot-directory>")
        sys.exit(-1)

    topdir = Path(sys.argv[1]).absolute()

    def handlelink(filep, subdir):
        link = filep.readlink()
        if str(link)[0] != "/":
            return
        if link.startswith(topdir):
            return
        relpath = os.path.relpath((topdir / link).resolve(), subdir)
        os.unlink(filep)
        os.symlink(relpath, filep)

    for f in topdir.glob("**/*"):
        if f.is_file() and f.is_symlink():
            handlelink(f, f.parent)

Setting up the toolchain
~~~~~~~~~~~~~~~~~~~~~~~~

To perform the cross compilation, you need a special set of compilers,
libraries, and headers, which runs on the host architecture, but generates
(binaries/executables) for a target architecture.
For example, from x86_64 to aarch64.

It is recommended to use the official 10.2 `ARM Developer cross compilers`_,
which you can find on their official website. For this tutorial, we choose
``aarch64`` target architecture and we will assume that you downloaded the
`gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz`_ file,
and unpacked it.

With those compilers, now you need a CMake toolchain file. This is
a configuration file to set the compilers and sysroot information, together
with extra options like compilation flags, and other details. You can use the
following file as an example, but keep in mind they might vary:

.. code-block:: cmake

    # toolchain-aarch64.cmake
    cmake_minimum_required(VERSION 3.22)
    include_guard(GLOBAL)

    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR aarch64)

    set(TARGET_SYSROOT /path/to/your/target/sysroot)
    set(CROSS_COMPILER /path/to/your/crosscompiling/compilers/)

    set(CMAKE_SYSROOT ${TARGET_SYSROOT})

    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig)
    set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})

    set(CMAKE_C_COMPILER ${CROSS_COMPILER}/aarch64-none-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER ${CROSS_COMPILER}/aarch64-none-linux-gnu-g++)

    set(QT_COMPILER_FLAGS "-march=armv8-a")
    set(QT_COMPILER_FLAGS_RELEASE "-O2 -pipe")
    set(QT_LINKER_FLAGS "-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed")

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    include(CMakeInitializeConfigs)

    function(cmake_initialize_per_config_variable _PREFIX _DOCSTRING)
      if (_PREFIX MATCHES "CMAKE_(C|CXX|ASM)_FLAGS")
        set(CMAKE_${CMAKE_MATCH_1}_FLAGS_INIT "${QT_COMPILER_FLAGS}")

        foreach (config DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
          if (DEFINED QT_COMPILER_FLAGS_${config})
            set(CMAKE_${CMAKE_MATCH_1}_FLAGS_${config}_INIT "${QT_COMPILER_FLAGS_${config}}")
          endif()
        endforeach()
      endif()

      if (_PREFIX MATCHES "CMAKE_(SHARED|MODULE|EXE)_LINKER_FLAGS")
        foreach (config SHARED MODULE EXE)
          set(CMAKE_${config}_LINKER_FLAGS_INIT "${QT_LINKER_FLAGS}")
        endforeach()
      endif()

      _cmake_initialize_per_config_variable(${ARGV})
    endfunction()

You need to adjust the paths in these two lines::

    set(TARGET_SYSROOT /path/to/your/target/sysroot)
    set(CROSS_COMPILER /path/to/your/crosscompiling/compilers/)

and replace them with the sysroot directory (the one we called ``rpi-sysroot``),
and the compilers (the ``gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin`` directory).


Cross compiling PySide
~~~~~~~~~~~~~~~~~~~~~~

After you have installed the prerequisites and copied the necessary files, you
should have the following:

* The compilers to cross compile (``gcc-argm-10.2-...``),
* The target sysroot (``rpi-sysroot``),
* The toolchain cmake file (``toolchain-aarch64.cmake``),
* The ``pyside-setup`` repository,

An example of the ``setup.py`` invocation might look like the following:

.. code-block:: bash

    python setup.py bdist_wheel \
        --parallel=8 --ignore-git --reuse-build --standalone --limited-api=yes \
        --cmake-toolchain-file=/opt/toolchain-aarch64.cmake \
        --qt-host-path=/opt/Qt/6.12.0/gcc_64 \
        --plat-name=linux_aarch64

Depending on the target platform, you could use ``linux_armv7``,
``linux_aarch64``, etc.

For embedded platforms, which typically do not have Qt and its tools fully
built, the option ``--no-qt-tools`` can be used to prevent the bundling of the
tools.

If the process succeeds, you will find the target wheels in your ``dist/``
directory, for example:

.. code-block:: bash

    PySide6-6.12.0-cp310-abi3-manylinux_2_42_aarch64.whl
    shiboken6-6.12.0cp310-abi3-manylinux_2_42_aarch64.whl


Troubleshooting
***************

* If the auto-detection mechanism fails to find the Python or Qt installations
  you have in your target device, you can use two additional options::

      --python-target-path=...

  and::

      --qt-target-path=...

* In case the automatic build of the host Shiboken (generator) fails,
  you can specify the custom path using::

      --shiboken-host-path=...

.. _`Manjaro ARM`: https://manjaro.org/products/download/arm
.. _`Archlinux ARM`: https://archlinuxarm.org
.. _`ARM Developer Cross Compilers`: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads
.. _`gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz`: https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz

.. _generating_sboms:

Generating SBOMs (Software Bill of Materials)
---------------------------------------------

|project| can generate Software Bill of Materials (SBOM) documents, which
describe the content of each wheel, system library dependencies and the tools
used to build the wheel. The documents are generated by the CMake build step of
the project, and are added to the ``.dist-info/sboms`` directory of each wheel,
as specified by `PEP 770`_.

Requirements
~~~~~~~~~~~~

In addition to the `General Requirements`_:

* **Qt:** 6.12.1+ for its CMake SBOM API.
* **Python packages:**

  * ``spdx-tools`` to generate SPDX v2.3 tag\:value and JSON documents
  * ``cyclonedx-python-lib`` to generate CycloneDX 1.6 JSON documents

  These can be installed by running::

    pip install -r requirements-sbom.txt

Enabling generation of SBOMs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To enable generation of SBOMs, pass the ``--sbom`` option to ``setup.py``. That
translates to the CMake option (``-DQT_GENERATE_SBOM=ON``), and the generated
documents are installed into ``<build-dir>/install/share/sbom/<document name>``::

    python setup.py build --qtpaths=/path/to/qtpaths --sbom

To ensure the documents are included in the wheels, pass ``--sbom`` to
``create_wheels.py`` as well::

    python create_wheels.py --build-dir=/directory/where/pyside/is/built --sbom

``python setup.py bdist_wheel`` and the Android and iOS cross-compilation
scripts also accept ``--sbom``.

What is included
~~~~~~~~~~~~~~~~

Each SBOM document is generated in three formats: SPDX 2.3 tag\:value
(``.spdx``), SPDX 2.3 JSON (``.spdx.json``) and CycloneDX 1.6 JSON
(``.cdx.json``).

Each wheel can contain one or more SBOM documents, depending on the contents of
the wheel.

The documents describe:

* the built libraries, Python modules and executables,
* the installed files, like ``.pyi`` files, typesystem files, headers and CMake
  files,
* the Qt tools shipped with the wheels,
* the dependencies: the Qt modules (as references to the documents of the Qt
  repositories), the Python runtime, and for ``shiboken6_generator``, libclang,
  libxml2 and libxslt,
* the build tools, like the Python interpreter and, on Android, the NDK and the
  SDK platform.

.. _`PEP 770`: https://peps.python.org/pep-0770/

.. _building_documentation:

Building the documentation
--------------------------

Starting from 6.3, the documentation is not being built by default.
When using the second option described in this section, make sure to use the
``--build-docs`` option.

Install the specific documentation requirements in your Python virtual
environment::

    pip install -r requirements-doc.txt

You can find the ``requirements-doc.txt`` file in the root of the repository.

Starting from 5.15, there are two options to build the documentation:

1. Building the base documentation (no API)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The process of parsing Qt headers to generate the PySide API documentation can take several
minutes, this means that modifying a specific section of the documentation we currently have, might
become a hard task. You may only care about the base documentation, which comprises all the
documentation except for the API documentation.

To generate this, execute the following command::

    python setup.py build_base_docs

This will generate an ``html/`` directory with the following structure::

    html
    └── pyside6
        ├── index.html
        ├── ...
        └── shiboken6
            ├── index.html
            └── ...

so you can open the main page ``html/pyside6/index.html`` on your browser to check the generated
files.

This is useful when updating the general sections of the documentation, adding tutorials,
modifying the build instructions, and more.

.. note:: In case you are interested in only generating the Example Gallery,
          you would need to run ``python tools/example_gallery/main.py`` to
          generate the examples ``documentation`` for the gallery. This will
          also be used internally by the ``build_base_docs`` target


2. Building the documentation (Base + API)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The documentation is being generated using **qdoc** to get the API information, and also **sphinx**
for the local Python related notes.

The system requires the ``libxml2`` and ``libxslt`` libraries.

After installing ``graphviz`` (included in the ``requirements-doc.txt`` file),
the ``dot`` command needs to be in PATH, otherwise,
the process will fail. Installing ``graphviz`` system-wide is also an option.

Since the process relies on a Qt installation, you need to specify where the
``qtbase`` directory of a Qt source tree is located by passing it to
the command line option ``--qt-src-dir``.

Once the common ``setup.py`` build process finishes (remember to use
``--build-docs`` to enable the documentation build, and ``--doc-build-online``
to get the HTML files), you can go to the generated
``build/<your_env_name>/build/pyside6`` directory, and run::

    ninja apidoc

You can add ``-j X``, to perform the build process in parallel with
X processes.

.. note:: The :command:`apidoc` make target builds offline documentation in ``QCH``
   (Qt Compressed Help) format by default. You can switch to building for the
   online use with the ``--doc-build-online`` configure option.

The target executes several steps:

#. The ``qdoc`` tool is run over the Qt source code to produce documentation in WebXML format.
#. ``shiboken6`` is run to extract the functions for which bindings exist from WebXML and convert it into RST.
#. ``sphinx`` is run to produce the documentation in HTML format.

Re-running the command will not execute step 1 unless the file
``qdoc-output/qtcore/webxml/qtcore-index.webxml`` is removed from the build tree.
Similarly, step 2 will not be executed unless the file ``base/PySide6/QtCore/index.rst``
is removed.

Finally, you will get a ``html`` directory containing all the generated documentation. The offline
help files, ``PySide.qch`` and ``Shiboken.qch``, can be moved to any directory of your choice. You
can find ``Shiboken.qch`` in the build directory, ``build/<your_env_name>/build/shiboken6/doc/html``.

If you want to temporarily change a ``.rst`` file to examine the impact on
formatting, you can re-run ``sphinx`` in the ``doc`` directory::

    sphinx-build base html

Viewing offline documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The offline documentation (QCH) can be viewed using the *Qt Creator* IDE or
*Qt Assistant*, which is a standalone application for viewing QCH files.

To view the QCH using *Qt Creator*, following the instructions outlined in
`Using Qt Creator Help Mode <https://doc.qt.io/qtcreator/creator-how-to-add-external-documentation.html>`_.
If you chose to use *Qt Assistant* instead, use the following command to register
the QCH file before launching *Qt Assistant*::

    assistant -register PySide.qch

Troubleshooting documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The documentation uses intersphinx to link from the PySide to the
Shiboken documentation. This can fail if

* the default ``QCH`` format is used; in which case the required ``objects.inv``
  files are not generated. Use ``--doc-build-online``.
* base and full doc builds are mixed, resulting in wrong values for the
  intersphinx location in the CMake files. Re-run ``cmake`` to fix this.

Using the internal tools
------------------------

A set of tools can be found under the ``tools/`` directory inside the ``pyside-setup`` repository.

* ``checklibs.py``: Script to analyze dynamic library dependencies of Mach-O binaries.
  To use this utility, just run::

    python checklibs.py /path/to/some.app/Contents/MacOS/Some

  This script was fetched from this repository_.

* ``create_changelog.py``: Script used to create the CHANGELOG that you can find in the ``dist/``
  directory. Usage::

    python create_changelog.py -r 6.12.1

* ``debug_windows.py``: This script can be used to find out why PySide modules
  fail to load with various DLL errors like Missing DLL or Missing symbol in DLL.

  You can think of it as a Windows version of :command:`ldd` / ``LD_DEBUG``.

  Underneath, it uses the :command:`cdb.exe` command line debugger and the :command:`gflags.exe`
  tool, which are installed with the latest Windows Kit.

  The aim is to help developers debug issues that they may encounter using the PySide imports on
  Windows. The user should then provide the generated log file.

  Incidentally it can also be used for any Windows executables, not just Python.
  To use it just run::

    python debug_windows.py

* ``missing_bindings.py``: This script is used to compare the state of PySide and PyQt
  regarding available modules and classses. This content is displayed in our `wiki page`_,
  and can be used as follows::

    python missing_bindings.py --qt-version 6.12.0 -w all

.. note:: The script relies on BeautifulSoup to parse the content and generate a list of the
   missing bindings.

.. _repository: https://github.com/liyanage/macosx-shell-scripts/
.. _`wiki page`: https://wiki.qt.io/Qt_for_Python_Missing_Bindings
.. _BeautifulSoup: https://www.crummy.com/software/BeautifulSoup/
.. _`CMake Unity Build Mode` : https://cmake.org/cmake/help/latest/prop_tgt/UNITY_BUILD.html
.. _`official Python website`: https://www.python.org/downloads/
.. _`Stable API`: https://docs.python.org/3/c-api/stable.html#stable
.. _`GH 134989`: https://github.com/python/cpython/issues/134989
.. _`online installer`: https://download.qt.io/official_releases/online_installers/
.. _`official CMake website`: https://cmake.org/download/
.. _`official Git website`: https://git-scm.com/downloads
.. _downloaded here: https://download.qt.io/development_releases/prebuilt/libclang/
.. _`Supported Platforms of Qt` : https://doc.qt.io/qt-6/supported-platforms.html
