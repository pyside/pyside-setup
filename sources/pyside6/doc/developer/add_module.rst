.. _developer-add-module:

Add a new module
================

New modules can be added for many reasons, the most important
one is when Qt enables or includes a new one for a new release.

Adding the bindings, and documentation are the essentials
to include new modules, but adding tests and examples is ideal.

Add bindings
------------

- Find the correct name (look at the include path of Qt).
- Add the module to the ``coin/dependencies.yaml`` file.
- Add it to ``sources/pyside6/cmake/PySideHelpers.cmake``.
- Add it to ``build_scripts/wheel_files.py`` (plugins, translations).
- Add it to ``sources/pyside6/cmake/PySideSbomWheelMapping.cmake`` for SBOM generation
- Copy an existing module to ``sources/pyside6/PySide6/<name>``.
- Adapt the ``typesystem.xml`` and ``CMakeList.txt`` (using for example
  *Qt Creator*'s case-preserving replace function).
- Make sure the dependencies are correct.
- Find the exported public classes, add them to the ``typesystem.xml`` file,
  checking whether they are ``value-type`` or ``object-type``. Add their enums
  and flags.
- Add the wrapper files to ``CMakeList.txt``.
- Create a test dir under ``sources/pyside6/tests`` with an empty
  ``CMakeList.txt``.
- Try to build with the module added to the ``--module-subset`` option of
  ``setup.py``.
- Watch out for shiboken warnings in the log.
- Be aware that ``ninja`` mixes stdout and stderr, so, the first warning is
  typically hidden behind a progress message.
- A convenient way of doing this is using
  ``qt-creator/scripts/shiboken2tasks.py`` from the
  `*Qt Creator* repository <https://code.qt.io/cgit/qt-creator/qt-creator.git>`_
  converting them to a ``.tasks`` file which can be loaded into *Qt Creator*'s
  issue pane.
- Link errors may manifest when ``generate_pyi`` imports the module trying
  to create signatures. They indicate a missing source file entry
  or a bug in the module itself.

.. note:: For the build to succeed, the module must follow the Qt convention
   of using ``#include <QtModule/header.h>`` since module include paths
   are not passed in PySide.

Distribution
------------

- Determine to which wheel the module belongs according to
  `Qt Modules <https://doc.qt.io/qt-6/qtmodules.html>`_.
- Add the module to ``build_scripts/wheel_files.py`` for use by
  ``create_wheels.py``.
- Add the module to one of the ``README.pyside6_*.md`` files.

Add documentation
-----------------

- Add entry to ``sources/pyside6/doc/modules.rst``.
- Add a .qdocconf.in file in ``sources/pyside6/doc/qtmodules``.
- Add module description ``.rst`` file in ``sources/pyside6/doc/extras`` if the
  auto-generated page looks unsatisfactory:

  * Run a full documentation build
  * Copy the auto-generated file ``<build>/pyside6/doc/base/<Module>/index.rst``
    to ``sources/pyside6/doc/extras/<Module>/<Module>.rst``
  * Remove TOC, qmake/CMake instructions and class lists (cf existing files)
  * Check the references, preferably link to converted documents
    in PySide or add documents there (see :ref:`maintaining-additionaldocs`).
