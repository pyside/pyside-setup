Specifying Types
----------------

Including Snippets
^^^^^^^^^^^^^^^^^^

There might be repetitive XML code, for example function modifications that
need to be done on classes that are not related by type inheritance.
It is possible to split out such snippets and include them via an entity reference.

.. code-block:: xml

    <typesystem>
        <object-type name="A">
            &common_function_modifications;
        </object-type>
        <object-type name="B">
            &common_function_modifications;
        </object-type>
    </typesystem>

The entity name is interpreted as file name (with suffix **xml**) appended and resolved
in the type system paths passed as command line argument.

Note that this is not a standard externally parsed entity due to the limitations
of the underlying parser.

.. _typesystem_details:

typesystem
^^^^^^^^^^

This is the root node containing all the type system information.
It may contain :ref:`add-function`, :ref:`container-type`,
:ref:`custom-type`, :ref:`enum-type`, :ref:`extra-includes`, :ref:`function`,
:ref:`load-typesystem`, :ref:`namespace`, :ref:`object-type`,
:ref:`opaque-container`, :ref:`overload-removal`,
:ref:`primitive-type`, :ref:`rejection`, :ref:`smart-pointer-type`,
:ref:`suppress-warning`, :ref:`template`, :ref:`system_include`,
:ref:`typedef-type` or :ref:`value-type` child nodes.

It can have a number of attributes, described below.

.. code-block:: xml

    <typesystem package="..."
                submodule-of="..."
                allow-thread="..."
                exception-handling="..."
                snake-case="yes | no | both"
                namespace-begin="..."
                namespace-end="..."
                doc-package="..."
                doc-mode = "nested | flat"
                module-uses-gil="yes | no" >
    </typesystem>

The **package** attribute is a string describing the package to be used,
e.g. "QtCore".

The *optional* **submodule-of** attribute specifies the name of a module to
which the module is added as a sub-module. This requires adapting the
installation directory of the module accordingly.

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **snake-case** attribute specifies whether function
and field names will be automatically changed to the snake case
style that is common in Python (for example, ``snakeCase`` will be
changed to ``snake_case``).

The value ``both`` means that the function or field will be exposed
under both its original name and the snake case version. There are
limitations to this though:

- When overriding a virtual function of a C++ class in Python,
  the snake case name must be used.

- When static and non-static overloads of a class member function
  exist (as is the case for example for ``QFileInfo::exists()``),
  the snake case name must be used.

The *optional* **namespace-begin** and **namespace-end** attributes will be
generated around the forward declarations in the module header. This is
intended for libraries which can optionally use inline namespaces
to allow for linking several versions of them together.
For example, for *Qt*, one would specify ``QT_BEGIN_NAMESPACE``,
``QT_END_NAMESPACE``, respectively.

The *optional* attribute **doc-package** specifies an alternate package name
for the documentation. This is used for finding the ``qdoc`` or ``doxygen``
files for the documentation generation. It is mainly relevant for Qt where for
example the documentation of the ``QtMultimediaWidgets`` module is generated
into the ``QtMultimedia`` module.

The *optional* attribute **doc-mode** determines whether to nest the
documentation of inner classes into the outer classes' pages. Its default value
is **nested**. It is possible to turn this off by specifying **flat** in case
too many classes are contained in a namespace, causing excessively long pages.
However, the referencing may then not fully work.

The *optional* attribute **module-uses-gil** determines whether the module uses
the *Global Interpreter Lock* (GIL) in a *Free Threaded* build.

.. note:: *Free Threaded* builds are not supported.

.. _load-typesystem:

load-typesystem
^^^^^^^^^^^^^^^

The ``load-typesystem`` node specifies which type systems to load when mapping
multiple libraries to another language or basing one library on another, and
it is a child of the :ref:`typesystem_details` node.

.. code-block:: xml

    <typesystem>
        <load-typesystem name="..." generate="yes | no" />
    </typesystem>

The **name** attribute is the filename of the typesystem to load, the
**generate** attribute specifies whether code should be generated or not. The
later must be specified when basing one library on another, making the generator
able to understand inheritance hierarchies, primitive mapping, parameter types
in functions, etc.

Most libraries will be based on both the QtCore and QtGui modules, in which
case code generation for these libraries will be disabled.

.. _rejection:

rejection
^^^^^^^^^

The ``rejection`` node rejects the given class, or the specified function
or field, and it is a child of the :ref:`typesystem_details` node.

.. code-block:: xml

    <typesystem>
        <rejection class="..."
            function-name="..."
            argument-type="..."
            field-name="..." />
    </typesystem>

The **class** attribute is the C++ class name of the class to reject. Use
the *optional* **function-name**, **argument-type**, or **field-name**
attributes to reject a particular function, function with arguments of a
particular type, or a field. Note that the **field-name** and
**function-name**/**argument-type** cannot be specified at the same time.
To remove all occurrences of a given field or function, set the class
attribute to \*.

.. _primitive-type:

primitive-type
^^^^^^^^^^^^^^

The ``primitive-type`` node describes how a primitive type is mapped from C++ to
the target language, and is a child of the :ref:`typesystem_details` node. It may
contain :ref:`conversion-rule` child nodes. Note that most primitives are
already specified in the QtCore typesystem (see :ref:`primitive-cpp-types`).

.. code-block:: xml

    <typesystem>
        <primitive-type name="..."
            since="..."
            until="..."
            target-lang-api-name="..."
            default-constructor="..."
            preferred-conversion="yes | no"
            view-on="..." />
    </typesystem>

The **name** attribute is the name of the primitive in C++.

The optional **target-lang-api-name** attribute is the name of the
primitive type in the target language, defaulting to the **name** attribute.

The *optional* **since** value is used to specify the API version in which
the type was introduced.

Similarly, the *optional* **until** value can be used to specify the API
version in which the type will be obsoleted.

If the *optional* **preferred-conversion** attribute is set to *no*, it
indicates that this version of the primitive type is not the preferred C++
equivalent of the target language type. For example, in Python both "qint64"
and "long long" become "long" but we should prefer the "qint64" version. For
this reason we mark "long long" with preferred-conversion="no".

The *optional* **default-constructor** specifies the minimal constructor
call to build one value of the primitive-type. This is not needed when the
primitive-type may be built with a default constructor (the one without
arguments).

The *optional* **preferred-conversion** attribute tells how to build a default
instance of the primitive type. It should be a constructor call capable of
creating a instance of the primitive type. Example: a class "Foo" could have
a **preferred-conversion** value set to "Foo()". Usually this attribute is
used only for classes declared as primitive types and not for primitive C++
types, but that depends on the application using *ApiExtractor*.

The *optional* **view-on** attribute specifies that the type is a view
class like std::string_view or QStringView which has a constructor
accepting another type like std::string or QString. Since typically
no values can be assigned to view classes, no target-to-native conversion
can be generated for them. Instead, an instance of the viewed class should
be instantiated and passed to functions using the view class
for argument types.

It is also possible to specify template specializations
like "std::optional<int>" as primitive types with converters.

See :ref:`predefined_templates` for built-in templates for standard type
conversion rules.

.. _namespace:

namespace-type
^^^^^^^^^^^^^^

The ``namespace-type`` node maps the given C++ namespace to the target
language, and it is a child of the :ref:`typesystem_details` node or other
``namespace-type`` nodes. It may contain :ref:`add-function`,
:ref:`declare-function`, :ref:`enum-type`, :ref:`extra-includes`,
:ref:`include-element`, :ref:`modify-function`, ``namespace-type``,
:ref:`object-type`, :ref:`smart-pointer-type`, :ref:`typedef-type` or :ref:`value-type`
child nodes.

.. code-block:: xml

    <typesystem>
        <namespace-type name="..."
            visible="true | auto | false"
            generate="yes | no"
            generate-using="yes | no"
            package="..."
            since="..."
            extends = "..."
            files = "..."
            doc-file = "..."
            revision="..." />
    </typesystem>

The **name** attribute is the name of the namespace, e.g., "Qt".

The *optional* **visible** attribute is used specify whether the
namespace is visible in the target language name. Its default value is
**auto**. It means that normal namespaces are visible, but inline namespaces
(as introduced in C++ 11) will not be visible.

The detection of inline namespaces requires shiboken to be built
using LLVM 9.0.

The *optional* **generate** is a legacy attribute. Specifying
**no** is equivalent to **visible="false"**.

The *optional* **generate-using** attribute specifies whether
``using namespace`` is generated into the wrapper code for classes within
the namespace (default: **yes**). This ensures for example that not fully
qualified enumeration values of default argument values compile.
However, in rare cases, it might cause ambiguities and can then be turned
off.

The **package** attribute can be used to override the package of the type system.

The *optional* **since** value is used to specify the API version of this type.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The *optional* **extends** attribute specifies the module name where the given
namespace first occurs in case of a namespace spanning several modules. For
example, in Qt, the namespace ``Qt`` first occurs in the ``QtCore`` module and
is further populated in the ``QtGui`` module. ``QtGui.Qt`` will then be
generated extending ``QtCore.Qt`` if **extends** is specified.

The *optional* **file** attribute specifies a regular expression matching the
include files whose contents are to be associated with the current module in
case of a namespace spanning several modules.

The *optional* **doc-file** attribute specifies the base name of a ``.qdoc``
file where documentation of the class is to be found (``qdoc`` only).

.. _enum-type:

enum-type
^^^^^^^^^

The ``enum-type`` node maps the given enum from C++ to the target language,
and it is a child of the :ref:`typesystem_details` node. Use
:ref:`reject-enum-value` child nodes to reject values.

.. code-block:: xml

    <typesystem>
        <enum-type name="..."
            identified-by-value="..."
            class="yes | no"
            since="..."
            flags="yes | no"
            flags-revision="..."
            cpp-type = "..."
            doc-file = "..."
            python-type = "IntEnum | IntFlag"
            lower-bound="..."
            upper-bound="..."
            force-unsigned="yes | no"
            extensible="yes | no"
            revision="..." />
    </typesystem>

The **name** attribute is the fully qualified C++ name of the enum
(e.g.,"Qt::FillRule"). If the *optional* **flags** attribute is set to *yes*
(the default is *no*), the generator will expect an existing QFlags<T> for the
given enum type.

The *optional* **force-unsigned** attribute forces to the underlying
enum type to be unsigned.

The *optional*  **since** value is used to specify the API version of this type.

The attribute **identified-by-value** helps to specify anonymous enums using the
name of one of their values, which is unique for the anonymous enum scope.
Notice that the **enum-type** tag can either have **name** or **identified-by-value**
but not both.

The *optional* **python-type** attribute specifies the underlying
Python type.

The *optional* **cpp-type** attribute specifies a C++ to be used for
casting values. This can be useful for large values triggering MSVC
signedness issues.

The *optional* **doc-file** attribute specifies the base name of the C++ or
``.qdoc`` file indicated by ``\relates`` or ``\headerfile`` in ``qdoc``, to
which the documentation of the enumeration is to be added and displayed in the
case its a global enumeration. This attribute is for ``qdoc`` only.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The **flags-revision** attribute has the same purposes of **revision** attribute but
is used for the QFlag related to this enum.

An enum can also be a C++ type alias:

.. code-block:: c++

    enum Option { Value1 = 0; }

    class SomeClass {
        public:
        using OptionAlias = Option;
    };

In this case, when specifying `<enum-type name="OptionAlias"...>` in
`SomeClass`, an enumeration `OptionAlias` will be generated into the class. The
values of `OptionAlias` and `Option` can be used interchangeably. This feature
is specifically intended for renaming enumerations by deprecating; it works for
at most one alias.

.. _reject-enum-value:

reject-enum-value
^^^^^^^^^^^^^^^^^

The ``reject-enum-value`` node rejects the enum value specified by the
**name** attribute, and it is a child of the :ref:`enum-type` node.

.. code-block:: xml

     <enum-type>
         <reject-enum-value name="..."/>
     </enum-type>

This node is used when a C++ enum implementation has several identical numeric
values, some of which are typically obsolete.

.. _value-type:

value-type
^^^^^^^^^^

The ``value-type`` node indicates that the given C++ type is mapped onto the
target language as a value type (see :ref:`objects-values`). This means that it
is an object passed by value on C++, i.e. it is stored in the function call
stack.

It is a child of the :ref:`typesystem_details`
node or other type nodes and may contain :ref:`add-function`, :ref:`add-pymethoddef`,
:ref:`configuration-element`, :ref:`declare-function`, :ref:`conversion-rule`,
:ref:`enum-type`, :ref:`extra-includes`, :ref:`include-element`, :ref:`modify-function`,
:ref:`object-type`, :ref:`smart-pointer-type`, :ref:`typedef-type` or further
``value-type`` child nodes.

.. code-block:: xml

    <typesystem>
        <value-type name="..." since="..."
         default-constructible="yes | no"
         copyable="yes | no"
         movable="yes | no"
         destructible="yes | no"
         allow-thread="..."
         disable-wrapper="yes | no"
         exception-handling="..."
         generate-functions="..."
         isNull ="yes | no"
         operator-bool="yes | no"
         hash-function="..."
         private="yes | no"
         qt-register-metatype = "yes | no | base"
         stream="yes | no"
         default-constructor="..."
         revision="..."
         snake-case="yes | no | both"
         doc-file = "..." />
    </typesystem>

The **name** attribute is the fully qualified C++ class name, such as
"QMatrix" or "QPainterPath::Element". The *optional* **hash-function**
attribute informs the function name of a hash function for the type.

The *optional* **default-constructible** attribute is used to override the
built-in detection of the default constructor.

The *optional* **default-constructor** specifies the minimal constructor
call to build one instance of the value-type. This is not needed when the
value-type may be built with a default constructor (the one without arguments).
Usually a code generator may guess a minimal constructor for a value-type based
on its constructor signatures, thus **default-constructor** is used only in
very odd cases.

The *optional* **copyable** attribute is used to override the built-in
detection of the copy constructor. This also implies the type has an assignment
operator; which is important for the copy conversion.

The *optional* **movable** attribute is used to specify whether the type has a
move constructor. The default is "yes"; there is no auto-detection.

The *optional* **destructible** attribute is used to override the built-in
(simple) detection of the destructibility. Unless :ref:`avoid-protected-hack`
is given or mandated by the compiler, classes with protected destructors
are considered destructible.

The *optional* attribute **stream** specifies whether this type will be able to
use externally defined operators, like QDataStream << and >>. If equals to **yes**,
these operators will be called as normal methods within the current class.

The *optional* **since** value is used to specify the API version of this type.

For the *optional* **disable-wrapper** and **generate-functions**
attributes, see :ref:`object-type`.

For the *optional* **private** attribute, see :ref:`private_types`.

The *optional* **qt-register-metatype** attribute determines whether
a Qt meta type registration is generated for ``name``. By
default, this is generated for non-abstract, default-constructible
types for usage in signals and slots.
The value ``base`` means that the registration will be generated for the
class in question but not for inheriting classes. This allows for
restricting the registration to base classes of type hierarchies.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **snake-case** attribute allows for overriding the value
specified on the **typesystem** element.

The *optional* **isNull** and **operator-bool** attributes can be used
to override the command line setting for generating bool casts
(see :ref:`bool-cast`).

The *optional* **doc-file** attribute specifies the base name of a ``.qdoc``
file where documentation of the class is to be found (``qdoc`` only).

.. _object-type:

object-type
^^^^^^^^^^^

The object-type node indicates that the given C++ type is mapped onto the
target language as an object type (see :ref:`objects-values`). This means that
it is an object passed by pointer on C++ and it is stored on the heap.

It is a child of the :ref:`typesystem_details` node or other type nodes and may
contain :ref:`add-function`, :ref:`add-pymethoddef`,
:ref:`configuration-element`, :ref:`declare-function`, :ref:`enum-type`,
:ref:`extra-includes`, :ref:`include-element`, :ref:`modify-function`,
``object-type``, :ref:`smart-pointer-type`, :ref:`typedef-type` or
:ref:`value-type` child nodes.

.. code-block:: xml

    <typesystem>
        <object-type name="..."
         since="..."
         copyable="yes | no"
         allow-thread="..."
         delete-in-main-thread="yes | no"
         delete-in-owner-thread="yes | no"
         disable-wrapper="yes | no"
         exception-handling="..."
         generate-functions="..."
         force-abstract="yes | no"
         hash-function="..."
         isNull ="yes | no"
         operator-bool="yes | no"
         parent-management="yes | no"
         polymorphic-id-expression="..."
         polymorphic-name-function="..."
         polymorphic-base="yes | no"
         private="yes | no"
         qt-metaobject="yes | no"
         qt-register-metatype = "yes | no | base"
         stream="yes | no"
         revision="..."
         snake-case="yes | no | both"
         doc-file = "..." />
    </typesystem>

The **name** attribute is the fully qualified C++ class name. The
**copyable** and **hash-function** attributes are the same as described for
:ref:`value-type`.

The *optional* **default-superclass** attribute can be used to specify a
superclass for the given type in the generated target language API.
This can be useful if the C++ base class is not exposed. The specified
super class needs to be a direct base class of the class in question.

The *optional* **delete-in-main-thread** attribute specifies that the
destructor is called in the main thread (scheduled via
``Py_AddPendingCall()``). This is intended to ensure that Qt's Widget classes
are deleted in the main GUI thread even when a garbage collection running in a
different thread cleans them up.

The *optional* **delete-in-owner-thread** attribute (applicable to Qt's
``QObject``-derived types only) specifies that destruction is deferred by
calling ``deleteLater()`` in case a garbage collection running in a thread
different from the owner thread of the instance cleans them up.

The *optional* **force-abstract** attribute forces the class to be
abstract, disabling its instantiation. The generator will normally detect
this automatically unless the class inherits from an abstract base class
that is not in the type system.

The *optional* **disable-wrapper** attribute disables the generation of a
**C++ Wrapper** (see :ref:`codegenerationterminology`). This will
effectively disable overriding virtuals methods in Python for the class.
It can be used when the class cannot be instantiated from Python and
its virtual methods pose some problem for the code generator (by returning
references, or using a default value that cannot be generated for a
parameter, or similar).

For the *optional* **private** attribute, see :ref:`private_types`.

The *optional* **qt-metaobject** attribute specifies whether
the special Qt virtual functions ``metaObject()``,
``metaCall()``, and ``metaCast()`` are generated. For classes
using dynamic meta objects, for example, ``QDBusInterface``,
it can be turned off.

The *optional* **qt-register-metatype** attribute determines whether
a Qt meta type registration is generated for ``name *``. By
default, this is only generated for non-QObject types for usage
in signals and slots.
The value ``base`` means that the registration will be generated for the
class in question but not for inheriting classes. This allows for
restricting the registration to base classes of type hierarchies.

The *optional* attribute **stream** specifies whether this type will be able to
use externally defined operators, like QDataStream << and >>. If equals to **yes**,
these operators will be called as normal methods within the current class.

The *optional* **since** value is used to specify the API version of this type.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **generate-functions** specifies a semicolon-separated
list of function names or minimal signatures to be generated.
This allows for restricting the functions for which bindings are generated.
This also applies to virtual functions; so, all abstract functions
need to be listed to prevent non-compiling code to be generated.
If nothing is specified, bindings for all suitable functions are
generated. Note that special functions (constructors, etc),
cannot be specified.

The *optional* **snake-case** attribute allows for overriding the value
specified on the **typesystem** element.

The *optional* **isNull** and **operator-bool** attributes can be used
to override the command line setting for generating bool casts
(see :ref:`bool-cast`).

The *optional* **parent-management** attribute specifies that the class is
used for building object trees consisting of parents and children, for
example, user interfaces like the ``QWidget`` classes. For those classes,
the heuristics enabled by :ref:`ownership-parent-heuristics` and
:ref:`return-value-heuristics` are applied to automatically set parent
relationships. Compatibility note: In shiboken 6, when no class of the
type system has this attribute set, the heuristics will be applied
to all classes. In shiboken 7, it will be mandatory to set the
attribute.

For the *optional* **polymorphic-id-expression**, **polymorphic-name-function**
and **polymorphic-base** attributes, see :ref:`typediscovery-attributes`.

The *optional* **doc-file** attribute specifies the base name of a ``.qdoc``
file where documentation of the class is to be found (``qdoc`` only).

interface-type
^^^^^^^^^^^^^^

This type is deprecated and no longer has any effect. Use object-type instead.

.. _container-type:

container-type
^^^^^^^^^^^^^^

The ``container-type`` node indicates that the given class is a container and
must be handled using one of the conversion helpers provided by attribute **type**.
It is a child of the :ref:`typesystem_details` node and may contain
:ref:`conversion-rule` child nodes.

.. code-block:: xml

    <typesystem>
        <container-type name="..."
            since="..."
            type ="..."
            opaque-containers ="..." />
    </typesystem>

The **name** attribute is the fully qualified C++ class name. The **type**
attribute is used to indicate what conversion rule will be applied to the
container. It can be one of: *list*, *set*, *map*, *multi-map* or *pair*.

Some types were deprecated in 6.2: *string-list*, *linked-list*, *vector*,
*stack* and *queue* are equivalent to *list*. *hash* and *multi-hash*
are equivalent to *map* and *multi-map*, respectively.

The *optional* **opaque-containers** attribute specifies a semi-colon separated
list of mappings from instantiations to a type name for
:ref:`opaque-containers`:

.. code-block:: xml

    <typesystem>
        <container-type name="std::array"
                        opaque-containers ="int,3:IntArray3;float,4:FloatArray4">


The *optional* **since** value is used to specify the API version of this container.

Some common standard containers are :ref:`built-in <builtin-cpp-container-types>`,
and there are also a number of useful
:ref:`predefined conversion templates <predefined_templates>`.

.. _opaque-container:

opaque-container
^^^^^^^^^^^^^^^^

The ``opaque-container`` element can be used to add further instantiations
of :ref:`opaque containers <opaque-containers>` to existing container types
(built-in or specified by :ref:`container-type` in included modules).
It is a child of the :ref:`typesystem_details` node.

.. code-block:: xml

    <typesystem>
        <opaque-container name="..." opaque-containers ="..." />
    </typesystem>

For the **name** and **opaque-containers** attributes,
see :ref:`container-type`.

.. _typedef-type:

typedef-type
^^^^^^^^^^^^

The ``typedef-type`` node allows for specifying typedefs in the typesystem. They
are mostly equivalent to spelling out the typedef in the included header, which
is often complicated when trying to wrap libraries whose source code cannot be
easily extended.
It is a child of the :ref:`typesystem_details` node or other type nodes.

.. code-block:: xml

    <typesystem>
        <typedef-type name="..."
            source="..."
            since="..." />
    </typesystem>

The **source** attribute is the source. Example:

.. code-block:: xml

    <namespace-type name='std'>
        <value-type name='optional' generate='no'/>\n"
    </namespace-type>
    <typedef-type name="IntOptional" source="std::optional&lt;int&gt;"/>

is equivalent to

.. code-block:: c++

    typedef std::optional<int> IntOptional;

The *optional* **since** value is used to specify the API version of this type.

.. _custom-type:

custom-type
^^^^^^^^^^^

The ``custom-type`` node simply makes the parser aware of the existence of a target
language type, thus avoiding errors when trying to find a type used in function
signatures and other places. The proper handling of the custom type is meant to
be done by a generator using the APIExractor.
It is a child of the :ref:`typesystem_details` node.

.. code-block:: xml

    <typesystem>
        <custom-type name="..."
            check-function="..." />
    </typesystem>

The **name** attribute is the name of the custom type, e.g., "PyObject".

The *optional* **check-function** attribute can be used to specify a
boolean check function that verifies if the PyObject is of the given type
in the function overload decisor. While shiboken knows common check
functions like ``PyLong_Check()`` or ``PyType_Check()``, it might be useful
to provide one for function arguments modified to be custom types
handled by injected code (see :ref:`replace-type`).

See :ref:`cpython-types` for built-in types.

.. _smart-pointer-type:

smart-pointer-type
^^^^^^^^^^^^^^^^^^

The ``smart pointer`` type node indicates that the given class is a smart pointer
and requires inserting calls to **getter** to access the pointeee.
Currently, the usage is limited to function return values.
**ref-count-method** specifies the name of the method used to do reference counting.
It is a child of the :ref:`typesystem_details` node or other type nodes
and may contain :ref:`conversion-rule` nodes.

The *optional* attribute **instantiations** specifies for which instantiations
of the smart pointer wrappers will be generated (comma-separated list).
By default, this will happen for all instantiations found by code parsing.
This might be a problem when linking different modules, since wrappers for the
same instantiation might be generated into different modules, which then clash.
Providing an instantiations list makes it possible to specify which wrappers
will be generated into specific modules.

.. code-block:: xml

    <typesystem>
        <smart-pointer-type name="..."
            since="..."
            type="shared | handle | value-handle | unique"
            getter="..."
            ref-count-method="..."
            value-check-method="..."
            null-check-method="..."
            reset-method="..."
            instantiations="..."
            excluded-instantiations="..."/>
            to-python="default"
        </typesystem>


The *optional* attribute **value-check-method** specifies a method
that can be used to check whether the pointer has a value.

The *optional* attribute **null-check-method** specifies a method
that can be used to check for ``nullptr``.

The *optional* attribute **reset-method** specifies a method
that can be used to clear the pointer.

The *optional* **instantiations** attribute specifies a comma-separated
list of instantiation types. When left empty, all instantiations
found in the code will be generated. The type name might optionally
be followed an equal sign and the Python type name, for example
``instantiations="int=IntPtr,double=DoublePtr"``.
It is also possible to specify a namespace delimited by ``::``.
By default, the type will be in the namespace of the smart pointer,
for example, ``std`` for ``std::shared_ptr``. Preceding
the type name by ``::`` causes it to be in the global namespace.

The *optional* **excluded-instantiations** attribute specifies a
comma-separated list of types to be excluded from instantiating. Typically,
this is used to exclude instantiations present in an underlying base module to
prevent symbol clashes.

The *optional* **to-python** attribute specifies how a smart pointer
instance is converted to Python:

.. list-table::
  :header-rows: 1

  * - Value

    - Meaning

  * - ``default``

    - A smart pointer instance is returned in all cases

  * - ``null-as-none``

    - ``None`` is returned if the smart pointer is null.

The *optional* attribute **type** specifies the type:

*shared*
   A standard shared pointer.
*handle*
   A basic pointer handle which has a getter function and an
   ``operator->``.
*value-handle*
   A handle which has a getter function returning a value
   (``T`` instead of ``T *`` as for the other types).
   It can be used for ``std::optional``.
*unique*
   A standard, unique pointer (``std::unique_ptr``) or a similar
   movable pointer.
   Specifying the ``reset-method`` attribute is required for this work.

The example below shows an entry for a ``std::shared_ptr``:

.. code-block:: xml

    <system-include file-name="memory"/>

    <namespace-type name="std">
        <include file-name="memory" location="global"/>
        <modify-function signature="^.*$" remove="all"/>
        <enum-type name="pointer_safety"/>
        <smart-pointer-type name="shared_ptr" type="shared" getter="get"
                            ref-count-method="use_count"
                            instantiations="Integer">
            <include file-name="memory" location="global"/>
        </smart-pointer-type>
    </namespace-type>

If the smart pointer is the only relevant class from namespace ``std``,
it can also be hidden:

.. code-block:: xml

    <namespace-type name="std" visible="no">
        <smart-pointer-type name="shared_ptr" type="shared" getter="get"
                            ref-count-method="use_count"
                            instantiations="Integer">
            <include file-name="memory" location="global"/>
        </smart-pointer-type>
    </namespace-type>

First, shiboken is told to actually parse the system include files
containing the class definition using the :ref:`system_include`
element. For the ``namespace-type`` and ``smart-pointer-type``, the
standard include files are given to override the internal implementation
header ``shared_ptr.h``.
This creates some wrapper sources which need to be added to the
``CMakeLists.txt`` of the module.

.. _function:

function
^^^^^^^^

The ``function`` node indicates that the given C++ global function is mapped
onto the target language. It is a child of the :ref:`typesystem_details` node
and may contain a :ref:`modify-function` child node.

.. code-block:: xml

    <typesystem>
        <function signature="..." rename="..." since="..."
                  allow-thread="true | auto | false"
                  doc-file = "..."
                  exception-handling="off | auto-off | auto-on | on"
                  overload-number="number"
                  snake-case="yes | no | both" />
    </typesystem>

There is a limitation; you cannot add a function overload using
the :ref:`add-function` tag to an existent function.

The *optional* **since** attribute is used to specify the API version in which
the function was introduced.

The *optional* **rename** attribute is used to modify the function name.

The *optional* **doc-file** attribute specifies the base name of the C++ or
``.qdoc`` file indicated by ``\relates`` or ``\headerfile`` in ``qdoc``, to
which the documentation of the function is to be added and displayed in the
case its a global function. This attribute is for ``qdoc`` only.

For the *optional* attributes **allow-thread**, **exception-handling**,
**overload-number** and **snake-case**, see :ref:`modify-function`.

.. _system_include:

system-include
^^^^^^^^^^^^^^

The optional **system-include** specifies the name of a system include
file or a system include path (indicated by a trailing slash) to be
parsed. Normally, include files considered to be system include
files are skipped by the C++ code parser. Its primary use case
is exposing classes from the STL library.
It is a child of the :ref:`typesystem_details` node.

.. code-block:: xml

    <typesystem>
        <system-include file-name="memory"/>
        <system-include file-name="/usr/include/Qt/"/>
    </typesystem>

.. _conditional_processing:

Conditional Processing
^^^^^^^^^^^^^^^^^^^^^^

Simple processing instructions are provided for including or excluding
sections depending on the presence of keywords. The syntax is:

.. code-block:: xml

    <?if keyword !excluded_keyword ?>
       ...
    <?endif?>

There are predefined keywords indicating the operating system (``windows``,
``unix`` and ``darwin``).

The language level passed to the ``language-level`` command line option
is reflected as ``c++11``, ``c++14``, ``c++17`` or ``c++20``.

The class names passed to the
:ref:`--drop-type-entries <drop-type-entries>` command line option
are also predefined, prefixed by ``no_``. This allows for example
for enclosing added functions referring to those classes within
``<?if !no_ClassName?>``, ``<?endif?>``.

Other keywords can be specified using the
:ref:`--keywords <conditional_keywords>` command line option.

Defining Entities
^^^^^^^^^^^^^^^^^

It is possible to define entities using a simple processing instruction:

.. code-block:: xml

    <?entity name value?>
    <text>&name;</text>

This allows for defining function signatures depending on platform
in conjunction with :ref:`conditional_processing`.

.. _private_types:

Private Types
^^^^^^^^^^^^^

Marking :ref:`object-type` or :ref:`value-type` entries as private causes a
separate, private module header besides the public module header to be
generated for them.

This can be used for classes that are not referenced in dependent modules
and helps to prevent the propagation of for example private C++ headers
required for them.
