.. _builtin-types:

Built-in Types
--------------

.. _primitive-cpp-types:

Primitive C++ Types
^^^^^^^^^^^^^^^^^^^

Shiboken knows the C++ primitive types like int and float and gathers
information about typedefs like `int32_t` and `size_t` at runtime while
parsing C++ headers. Function overloads using these types will be
automatically generated. To suppress a primitive type, use the
:ref:`rejection` tag.

In principle, there is no need to specify them in the typesystem
file using the :ref:`primitive-type` tag.

However, specifying a type means that the type name is used for
matching signatures of functions for :ref:`modification <modify-function>`.
So, it might make sense to specify architecture-dependent types like `size_t`
to avoid having to spell out the resolved type, which might differ depending
on platform.

`std::string`, `std::wstring` and their associated view types
`std::string_view`, `std::wstring_view` are also supported.


.. _builtin-cpp-container-types:

C++ Container Types
^^^^^^^^^^^^^^^^^^^

The C++ containers ``std::list``\, ``std::vector``\,
``std::pair``\, ``std::map``\, ``std::span`` and ``std::unordered_map`` are
built-in.
To specify :ref:`opaque-containers`, use the :ref:`opaque-container` element.
:ref:`container-type` can still be specified to modify the built-in behavior.
For this case, a number of pre-defined conversion templates
are provided (see :ref:`predefined_templates`).

.. _cpython-types:

CPython Types
^^^^^^^^^^^^^

Python types like `str` match CPython types like `PyUnicode` in the *Concrete
Objects Layer* of CPython. They have check functions like `PyUnicode_Check()`,
which Shiboken generates into the code checking the function arguments.

The CPython types are built into Shiboken as :ref:`custom types <custom-type>`
along with their check functions.

To generate functions taking such types as arguments, specify the CPython types
in the signature specified on :ref:`add-function` or as or `modified-type` on
:ref:`replace-type`.

This requires a code injection using the :ref:`pyarg` type system variable to
convert them to a C++ type for the call.

CPython types can also be used as type on `add-conversion` within a
:ref:`conversion-rule` or as target language API types on :ref:`primitive-type`.

.. list-table:: CPython Types

   * - Name
     - Description
   * - ``PyArrayObject``
     - `NumPy`_ array
   * - ``PyBool``
     - Boolean value
   * - ``PyBuffer``
     - A type implementing the `Buffer Protocol`_
   * - ``PyByteArray``
     - bytearray value
   * - ``PyBytes``
     - Bytes object
   * - ``PyCallable``
     - Callable object
   * - ``PyComplex``
     - Complex Number Object
   * - ``PyDate``
     - Date value
   * - ``PyDateTime``
     - Date time value
   * - ``PyDict``
     - Dictionary
   * - ``PyFloat``
     - Float value
   * - ``PyLong``
     - Long (integer) value
   * - ``PyObject``
     - Plain ``PyObject`` (typically used for optional parameters)
   * - ``PyPathLike``
     - Path from `pathlib`_ or string. The utility function
       ``PySide::pyPathToQString()`` (``pysideutils.h``) can be used to convert
       it to a ``QString``.
   * - ``PySequence``
     - Sequence
   * - ``PyTypeObject``
     - Type object
   * - ``PyUnicode``
     - Unicode object
   * - ``SbkChar``
     - Unicode object of length 1 (matching the ``char`` type of C++).
   * - ``str``
     - Unicode object or ``None``

.. _NumPy: https://numpy.org/
.. _`Buffer Protocol`: https://docs.python.org/3/c-api/buffer.html
.. _`pathlib`: https://docs.python.org/3/library/pathlib.html#module-pathlib
