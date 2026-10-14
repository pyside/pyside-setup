.. _gen-overview:

******************
Generator Overview
******************

The following diagram summarizes Shiboken's role in the Qt for Python
project.

.. image:: images/qtforpython-underthehood.png

An XML typesystem file is used to specify the types to be exposed to Python
and to apply modifications to properly represent and manipulate the types in
the Python World. For example, you can remove and add methods to certain types,
and also modify the arguments of each method. These actions are inevitable to
properly handle the data structures or types.

The final outcome of this process is a set of wrappers written in CPython,
which can be used as a module in your Python code.

In a few words, the Generator is a utility that parses a collection of header and
typesystem files, generating other files (code, documentation, etc.) as result.

Creating new bindings
=====================

.. figure:: images/bindinggen-development.png
   :scale: 80
   :align: center

   Creating new bindings

Each module of the generator system has an specific role.

1. Provide enough data about the classes and functions.
2. Generate valid code, with modifications from typesystems and injected codes.
3. Modify the API to expose the objects in a way that fits you target language best.
4. Insert customizations where handwritten code is needed.

.. figure:: images/shibokenqtarch.png
   :scale: 80
   :align: center

   Runtime architecture

The newly created binding will run on top of Shiboken which takes
care of interfacing Python and the underlying C++ library.

Handwritten inputs
==================

Creating new bindings involves creating several pieces of "code": the header,
the typesystem and, in most cases, the injected code.

**header** A header with ``#include`` directives listing all the headers of the
    desired classes. This header is not referenced by the generated code.
    Alternatively, it is possible to pass a list of the headers of the
    desired classes directly on the command line. In this case,
    the command line option ``--use-global-header`` should be passed as
    well to prevent the headers from being suppressed in the generated
    code.

:ref:`typesystem_details`: XML files that provides the developer with a tool to customize the
             way that the generators will see the classes and functions. For
             example, functions can be renamed, have its signature changed and
             many other actions.
:ref:`inject code <codeinjectionsemantics>`: allows the developer to insert
              handwritten code where the generated code is not suitable or
              needs some customization.

.. _command-line:

Command line options
********************

Usage
-----

::

   shiboken [options] header-file(s) typesystem-file


Options
-------

``--disable-verbose-error-messages``
    Disable verbose error messages. Turn the CPython code hard to debug but saves a few kilobytes
    in the generated binding.

.. _parent-heuristic:

``--enable-parent-ctor-heuristic``
    This flag enable an useful heuristic which can save a lot of work related to object ownership when
    writing the typesystem.
    For more info, check :ref:`ownership-parent-heuristics`.

.. _pyside-extensions:

``--enable-pyside-extensions``
    Enable pyside extensions like support for signal/slots. Use this if you are creating a binding based
    on PySide.

.. _return-heuristic:

``--enable-return-value-heuristic``
    Enable heuristics to detect parent relationship on return values.
    For more info, check :ref:`return-value-heuristics`.

.. _avoid-protected-hack:

``--avoid-protected-hack``
    Avoid the use of the '#define protected public' hack.

.. _use-isnull-as-nb-bool:

``--use-isnull-as-nb-bool``
    If a class has an isNull() const method, it will be used to
    compute the value of boolean casts (see :ref:`bool-cast`).
    The legacy option ``--use-isnull-as-nb_nonzero`` has the
    same effect, but should not be used any more.

``--lean-headers``
    Forward declare classes in module headers instead of including their class
    headers where possible.

.. _use-operator-bool-as-nb-bool:

``--use-operator-bool-as-nb-bool``
    If a class has an operator bool, it will be used to compute
    the value of boolean casts (see :ref:`bool-cast`).
    The legacy option ``--use-operator-bool-as-nb_nonzero`` has the
    same effect, but should not be used any more.

.. _no-implicit-conversions:

``--no-implicit-conversions``
    Do not generate implicit_conversions for function arguments.

.. _api-version:

``--api-version=<version>``
    Specify the supported api version used to generate the bindings.

.. _documentation-only:

``--documentation-only``
    Do not generate any code, just the documentation.

.. _drop-type-entries:

``--drop-type-entries="<TypeEntry0>[;TypeEntry1;...]"``
    Semicolon separated list of type system entries (classes, namespaces,
    global functions and enums) to be dropped from generation. Values are
    fully qualified Python type names ('Module.Class'), but the module can
    be omitted ('Class').

.. _conditional_keywords:

``-keywords=keyword1[,keyword2,...]``
    A comma-separated list of keywords for conditional typesystem parsing
    (see :ref:`conditional_processing`).

.. _module-uses-gil:

``--module-uses-gil=<yes|no|auto>``
    Whether the module uses the *Global Interpreter Lock* (GIL) in a *Free
    Threaded* build (default: yes). With **auto**, the generated code declares
    ``Py_MOD_GIL_NOT_USED`` only if ``Py_GIL_DISABLED`` is defined, and
    ``Py_MOD_GIL_USED`` otherwise (before Python 3.15, no ``Py_mod_gil`` slot,
    which means the same). The **module-uses-gil** attribute of the
    :ref:`typesystem <typesystem_details>` element overrides it.

``--use-global-header``
    Use the global headers passed on the command line in generated code.

.. _generation-set:

``--generation-set``
    Generator set to be used (e.g. qtdoc).

.. _skip-deprecated:

``--skip-deprecated``
    Skip deprecated functions.

.. _diff:

``--diff``
    Print a diff of wrapper files.

.. _dryrun:

``--dryrun``
    Dry run, do not generate wrapper files.

.. _--project-file:

``--project-file=<file>``
    Text file containing a description of the binding project.
    Replaces and overrides command line arguments.

.. _clang_option:

``--clang-option=<option>``
    Option to be passed to clang

.. _clang_options:

``--clang-options=<option1>[,<option2>,...]>``
    Options to be passed to clang.
    When '-' is passed as the first option in the list, none of the options
    built into shiboken will be added, allowing for a complete replacement.

.. _compiler-option:

``--compiler=<type>``
    Emulated compiler type (g++, msvc, clang)

.. _compiler-path-option:

``--compiler-path=<file>``
    Path to the compiler for determining builtin include paths

.. _compiler-argument-option:

``compiler-argument=<argument>``
    Add an argument for the compiler for determining builtin include paths

.. _platform-option:

``--platform=<name>``
    Emulated platform (``android``, ``darwin``, ``ios``, ``linux``, ``unix``, ``windows``).
    ``CMAKE_SYSTEM_NAME`` may be used.

.. _platform-version-option:

``--platform-version=<version>``
    Platform version

.. _arch-option:

``--arch=<name>``
    Emulated architecture (``x86_64``, ``arm64``, ``i586``).
    ``CMAKE_SYSTEM_PROCESSOR`` may be used.

.. _include-paths:

``-I<path>, --include-paths=<path>[:<path>:...]``
    Include paths used by the C++ parser.

.. _system-include-paths:

``-isystem<path>, --system-include-paths=<path>[:<path>:...]``
    System include paths used by the C++ parser

.. _dir-after-include-paths:

``-idirafter<path>, --dir-after-include-paths=<path>[:<path>:...]``
    Include paths searched after the system paths by the C++ parser. Note that
    the C++ parser considers headers found there to be system headers; use
    :ref:`force-process-system-include-paths` to extract types from them.

.. _framework-include-paths:

``-F<path>, --framework-include-paths=<path>[:<path>:...]``
    Framework include paths used by the C++ parser

.. _force-process-system-include-paths:

``--force-process-system-include-paths=<path>[:<path>:...]``
    Include paths that are considered as system headers by the C++ parser,
    but should still be processed to extract types

.. _language-level:

``--language-level=, -std=<level>``
    C++ Language level (c++11..c++17, default=c++14)

.. _typesystem-paths:

``-T<path>, --typesystem-paths=<path>[:<path>:...]``
    Paths used when searching for type system files.

.. _output-directory:

``--output-directory=[dir]``
    The directory where the generated files will be written.

.. _license-file=[license-file]:

``--license-file=[license-file]``
    File used for copyright headers of generated files.

.. _no-suppress-warnings:

``--no-suppress-warnings``
    Show all warnings.

``--log-unmatched``
    Prints :ref:`suppress-warning` and :ref:`rejection` elements that were
    not matched. This is useful for cleaning up old type system files.

.. _silent:

``--silent``
    Avoid printing any message.

.. _debug-level:

``--debug-level=[sparse|medium|full]``
    Set the debug level.

.. _help:

``--help``
    Display this help and exit.

``--print-builtin-types``
    Print information about builtin types

.. _version:

``--version``
    Output version information and exit.

QtDocGenerator Options
----------------------

.. _doc-parser:

``--doc-parser=<parser>``
    The documentation parser used to interpret the documentation
    input files (qdoc|doxygen).

.. _documentation-code-snippets-dir:

``--documentation-code-snippets-dir=<dir>``
    Directory used to search code snippets used by the documentation.

.. _documentation-data-dir:

``--documentation-data-dir=<dir>``
    Directory with XML files generated by documentation tool.

.. _documentation-extra-sections-dir=<dir>:

``--documentation-extra-sections-dir=<dir>``
    Directory used to search for extra documentation sections.

.. _library-source-dir:

``--library-source-dir=<dir>``
    Directory where library source code is located.

.. _additional-documentation:

``--additional-documentation=<file>``
   List of additional XML files to be converted to .rst files
   (for example, tutorials).

``--inheritance-file=<file>``
   Generate a JSON file containing the class inheritance.

``--disable-inheritance-diagram``
        Disable the generation of the inheritance diagram.

.. _project-file:

********************
Binding Project File
********************

Instead of directing the Generator behavior via command line, the binding
developer can write a text project file describing the same information, and
avoid the hassle of a long stream of command line arguments.

.. _project-file-structure:

The project file structure
==========================

Here follows a comprehensive example of a generator project file.

.. code-block:: ini

     [generator-project]
     generator-set = path/to/generator/CHOICE_GENERATOR
     header-file = DIR/global.h" />
     typesystem-file = DIR/typesystem_for_your_binding.xml
     output-directory location="OUTPUTDIR" />
     include-path = path/to/library/being/wrapped/headers/1
     include-path = path/to/library/being/wrapped/headers/2
     typesystem-path = path/to/directory/containing/type/system/files/1
     typesystem-path = path/to/directory/containing/type/system/files/2
     enable-parent-ctor-heuristic


Project file tags
=================

The generator project file tags are in direct relation to the
:ref:`command line arguments <command-line>`. All of the current command line
options provided by |project| were already seen on the
:ref:`project-file-structure`, for new command line options provided by
additional generator modules (e.g.: qtdoc, Shiboken) could also be used in the
generator project file following simple conversion rules.

For tags without options, just write as an empty tag without any attributes.
Example:

.. code-block:: bash

     --BOOLEAN-ARGUMENT

becomes

.. code-block:: ini

     BOOLEAN-ARGUMENT

and

.. code-block:: bash

     --VALUE-ARGUMENT=VALUE

becomes

.. code-block:: ini

     VALUE-ARGUMENT = VALUE


.. _cross-compilation:

Cross Compilation
=================

Shiboken uses **libclang** to parse the headers of the library to be exposed.
When compiling for another platform, the clang parser should ideally use the
target of the platform.

Simple bindings may already work when the parser uses the default host platform
target. But for bigger projects like Qt, it is important that macros like
``QT_POINTER_SIZE`` and the platform defines ``Q_OS_XXX`` are set correctly
when parsing files like ``qsystemdetection.h`` or ``qprocessordetection.h``.
Some Qt API might be excluded depending on platform and there might be subtle
differences depending on word size.

For platform and architecture, the relevant command line options are
:ref:`platform-option` and :ref:`arch-option`. They take common platform names
and architectures as used in target triplets and can be set to the values of
the CMake variables ``CMAKE_SYSTEM_NAME`` and ``CMAKE_SYSTEM_PROCESSOR``,
respectively. If the specified platform is different from the host, Shiboken
will pass a target triplet based on them to the clang parser.

Optionally, the version of the platform can be specified using the
:ref:`platform-version-option`. This is useful when the clang parser defaults
to a too-old version.

If this results in a wrong or too generic triplet, it is also possible to
directly pass a target triplet in the Clang options specified by
:ref:`clang_option`. In this case, Shiboken will not pass a target triplet and
try to derive the platform/architecture from this triplet.

When using the ``Clang`` and ``GNU`` compilers for cross-compiling, the
:ref:`compiler-path-option` option should be specified since Shiboken may need
to run the compiler to determine system include paths. For most cases, passing
the value of the CMake variable ``CMAKE_CXX_COMPILER`` should work. If the
compiler is in the path, it should suffice to pass the compiler type to
:ref:`compiler-option` (value of ``CMAKE_CXX_COMPILER_ID``).

It is possible (for example, when targeting Android) that ``CMAKE_CXX_COMPILER``
is a generic compiler that also needs a ``--target=`` or similar option to
locate the correct system include paths. In this case (shiboken failing due to
not finding some system headers), the :ref:`compiler-argument-option` can be
passed to specify the target.

Typically, a ``CMakeLists.txt`` files will then look like:

.. code-block:: cmake

    if (CMAKE_CROSSCOMPILING)
        list(APPEND shiboken_command "--platform=${CMAKE_SYSTEM_NAME}"
                                     "--arch=${CMAKE_SYSTEM_PROCESSOR}"
                                     "--compiler-path=${CMAKE_CXX_COMPILER}")
    endif()

When passing the target triplet:

.. code-block:: cmake

    if (CMAKE_CROSSCOMPILING)
        list(APPEND shiboken_command "--clang-option=--target=aarch64-none-linux-android"
                                     "--compiler-path=${CMAKE_CXX_COMPILER}")
    endif()

.. _Shiboken6Tools:

***********
CMake Usage
***********

The ``Shiboken6Tools`` CMake package provides an easy way to invoke the
Shiboken generator from CMake to create Python bindings for C++ libraries. It
is contained in the ``shiboken6_generator`` wheel. This is achieved using the
``shiboken_generator_create_binding`` CMake function. This function automates
the process of generating binding sources and building the Python extension
module.

Function Signature
==================

.. code-block:: cmake

    shiboken_generator_create_binding(
        TARGET_NAME <name>
        GENERATED_SOURCES <generated_sources>
        HEADERS <headers>
        TYPESYSTEM_FILE <typesystem_file>
        CPP_LIBRARY <cpp_library>
        [QT_MODULES <qt_modules>]
        [EXTRA_OPTIONS <extra_options>]
        [FORCE_LIMITED_API]
    )

Arguments
*********

* ``TARGET_NAME``: Name of the Python extension module target to create.
* ``GENERATED_SOURCES``: List of C++ source files generated by Shiboken.
* ``HEADERS``: List of C++ header files to parse.
* ``TYPESYSTEM_FILE``: Path to the typesystem XML file.
* ``CPP_LIBRARY``: C++ library to link against.
* ``QT_MODULES`` (optional): List of Qt modules required for the binding.
* ``EXTRA_OPTIONS`` (optional): Additional command line options for Shiboken.
* ``FORCE_LIMITED_API`` (optional): Use the Limited API for the generated extension module.

Usage Example
*************

.. code-block:: cmake

    shiboken_generator_create_binding(
        TARGET_NAME MyBinding
        GENERATED_SOURCES ${generated_sources}
        HEADERS ${wrapped_header}
        TYPESYSTEM_FILE ${typesystem_file}
        CPP_LIBRARY ${my_library}
        QT_MODULES Core Gui Widgets
        EXTRA_OPTIONS --some-extra-option
        FORCE_LIMITED_API
    )

This macro will generate the binding sources, build the Python module, and link it with the specified
libraries and include paths.

For complete usage examples, see:

* `SampleBinding Example <https://doc.qt.io/qtforpython-6/examples/example_samplebinding_samplebinding.html>`_
* `WidgetBinding Example <https://doc.qt.io/qtforpython-6/examples/example_widgetbinding_widgetbinding.html>`_
