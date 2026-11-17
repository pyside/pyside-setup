.. _manipulating-object-and-value-types:

Manipulating Object and Value Types
-----------------------------------

.. _inject-code:

inject-code
^^^^^^^^^^^

The ``inject-code`` node inserts the given code into the generated code for the
given type or function, and it is a child of the :ref:`object-type`, :ref:`value-type`,
:ref:`modify-function` and :ref:`add-function` nodes.
It may contain :ref:`insert-template` child nodes.

.. code-block:: xml

    <inject-code class="native | target"
                 position="beginning | end" since="..."
                 file="[file]"
                 snippet="[label]"/>


The ``class`` attribute specifies which module of the generated code that
will be affected by the code injection
(see :ref:`codegenerationterminology`). The ``class`` attribute accepts the
following values:

* ``native``: The c++ code
* ``target``: The binding code

If the ``position`` attribute is set to *beginning* (the default), the code
is inserted at the beginning of the function. If it is set to *end*, the code
is inserted at the end of the function.

For a detailed description of how to above attributes interact,
see :ref:`codeinjectionsemantics`.

The optional ``file`` attribute specifies the file name
(see :ref:`external-snippets`).

The optional ``snippet`` attribute specifies the snippet label
(see :ref:`external-snippets`).

There are a number of placeholders which are replaced when injecting
code (see :ref:`typesystemvariables`).

There are severals ways to specify the code:

Embedding Code into XML
=======================

The code can be embedded into XML (be careful to use the correct XML entities
for characters like '<', '>', '&'):

.. code-block:: xml

     <value-type>
         <inject-code class="native | target"
             position="beginning | end" since="...">
             // the code
         </inject-code>
     </value-type>


Using a Template Specified in XML
=================================

It is possible to create code templates for reuse in XML
(see :ref:`using-code-templates`). This allows for replacing of custom
placeholders.

.. code-block:: xml

     <value-type>
         <inject-code class="native | target" class="native | target">
             <insert-template name="template_name"/>
         </inject-code>
     </value-type>


.. _external-snippets:

Using Snippets From External Files
==================================

Code or documentation snippets can also be retrieved from external
files found in the typesystem search path (see :ref:`typesystem-paths`).

.. code-block:: xml

     <value-type>
         <inject-code class="native | target"
             position="beginning | end" since="..."
             file="external_source.cpp"
             snippet="label"/>
     </value-type>


In the external file ``external_source.cpp``, the code between annotations
of the form:

.. code-block:: c++

    // @snippet label
    ...
    // @snippet label


will be extracted.

.. _modify-field:

modify-field
^^^^^^^^^^^^

The ``modify-field`` node allows you to alter the access privileges for a given
C++ field when mapping it onto the target language, and it is a child of an
:ref:`object-type` or a :ref:`value-type` node.

.. code-block:: xml

     <object-type>
         <modify-field name="..."
             write="true | false"
             read="true | false"
             remove="true | false"
             opaque-container = "yes | no"
             snake-case="yes | no | both" />
     </object-type>

The ``name`` attribute is the name of the field, the *optional* ``write``
and ``read`` attributes specify the field's access privileges in the target
language API (both are set to true by default).

The ``remove`` attribute is an *optional* boolean attribute, which can
mark the field to be discarded on generation.

The *optional* ``rename`` attribute can be used to change the name of the
given field in the generated target language API.

The *optional* ``opaque-container`` attribute specifies whether
an opaque container should be returned on read access
(see :ref:`opaque-containers`).

The *optional* **snake-case** attribute allows for overriding the value
specified on the class entry or **typesystem** element.

.. _modify-function:

modify-function
^^^^^^^^^^^^^^^

The ``modify-function`` node allows you to modify a given C++ function when
mapping it onto the target language, and it is a child of a :ref:`function`,
:ref:`namespace`, :ref:`object-type` or a :ref:`value-type` node.
Nested :ref:`modify-argument` nodes can used to modify arguments
or return values.

.. code-block:: xml

     <object-type>
         <modify-function signature="..."
                          since="..."
                          remove="true | false"
                          access="public | private | protected"
                          allow-thread="true | auto | false"
                          blocking="true | false"
                          exception-handling="no | auto-off | auto-on | yes"
                          final="true | false"
                          overload-number="number"
                          rename="..."
                          snake-case="yes | no | both"
                          deprecated = "true | false"
                          disable-override-lookup-in-destructor = "true | false"/>
     </object-type>

The ``signature`` attribute is a normalized C++ signature, excluding return
values but including potential const declarations. It is not required
when ``modify-function`` appears as a child of a :ref:`function` node to
modify a global function.

The ``since`` attribute specify the API version when this function was modified.

The ``allow-thread`` attribute specifies whether a function should be wrapped
into ``Py_BEGIN_ALLOW_THREADS`` and ``Py_END_ALLOW_THREADS``, that is,
temporarily release the GIL (global interpreter lock). Doing so is required
for any thread-related function (wait operations), functions that might call
a virtual function (potentially reimplemented in Python), and recommended for
lengthy I/O operations or similar. It has performance costs, though.
The value ``auto`` means that it will be turned off for functions for which
it is deemed to be safe, for example, simple getters.
The attribute defaults to ``false``.

The optional ``blocking`` attribute specifies whether a function is blocking in
the sense that it drives an event loop from which methods overridden in Python
may be called repeatedly. If set to true, errors occurring in those overridden
methods are immediately printed and cleared instead of propagating to the outer
context, which may cause issues when processing the next event.
The attribute also applies to overridden functions in derived classes.

The ``exception-handling`` attribute specifies whether to generate exception
handling code (nest the function call into try / catch statements). It accepts
the following values:

* no, false: Do not generate exception handling code
* auto-off: Generate exception handling code for functions
  declaring a non-empty ``throw`` list
* auto-on: Generate exception handling code unless function
  declares ``noexcept``
* yes, true: Always generate exception handling code

The optional ``overload-number`` attribute specifies the position of the
overload when checking arguments. Typically, when a number of overloads
exists, as for in example in Qt:

.. code-block:: c++

    void QPainter::drawLine(QPointF, QPointF);
    void QPainter::drawLine(QPoint, QPoint);

they will be reordered such that the check for matching arguments for the
one taking a ``QPoint`` is done first. This is to avoid a potentially
costly implicit conversion from ``QPoint`` to ``QPointF`` when using the
2nd overload. There are cases though in which this is not desired;
most prominently when a class inherits from a container and overloads exist
for both types as is the case for the ``QPolygon`` class:

.. code-block:: c++

    class QPolygon : public QList<QPoint> {};

    void QPainter::drawPolygon(QPolygon);
    void QPainter::drawPolygon(QList<QPoint>);

By default, the overload taking a ``QList`` will be checked first, trying
to avoid constructing a ``QPolygon`` from ``QList``. The type check for a
list of points will succeed for a parameter of type ``QPolygon``, too,
since it inherits ``QList``. This presents a problem since the sequence
type check is costly due to it checking that each container element is a
``QPoint``. It is thus preferable to check for the ``QPolygon`` overload
first. This is achieved by specifying numbers as follows:

.. code-block:: xml

    <object-type name="QPainter">
        <modify-function signature="drawPolygon(QPolygon)" overload-number="0"/>
        <modify-function signature="drawPolygon(QList&lt;QPoint&gt;)" overload-number="1"/>
    </object-type>

Numbers should be given for all overloads; otherwise, the order will be in
declaration order.

The optional ``final`` attribute can be specified for virtual functions
and disables generating the code for overriding the function in Python
(native call). This is useful when the result type is not constructible.

The ``remove`` attribute is an *optional* boolean attribute, which can
mark the function to be discarded on generation.

The *optional* ``rename`` attribute can be used to change the name of the
given function in the generated target language API.

The *optional* ``access`` attribute changes the access privileges of the
given function in the generated target language API.

The *optional* **snake-case** attribute allows for overriding the value
specified on the class entry or **typesystem** element.

The *optional* **deprecated** attribute allows for overriding deprecation
as detected by the C++ attribute. It works in both ways.

The *optional* **disable-override-lookup-in-destructor** disables the lookup
of Python functions overriding a virtual C++ function when the wrapper
destructor runs. This avoids deadlocks between the *GIL* (taken by the lookup)
and other mutexes in the C++ library.

Specifically, this happens when *QObject* destructors disconnect
signal slot connections internally in Qt and thereby invoke virtuals like
*QObject::connectNotify()*  and *QObject::disconnectNotify()*, which lock
internal mutexes in *Qt*.

.. note:: Virtual functions should not normally be called from destructors in C++;
          this constitutes an issue in the library.

.. _add-function:

add-function
^^^^^^^^^^^^

The ``add-function`` node allows you to add a given function onto the target
language, and it is a child of an :ref:`object-type` or :ref:`value-type` nodes if the
function is supposed to be a method, or :ref:`namespace` and :ref:`typesystem_details` if
the function is supposed to be a function inside a namespace or a global function.
It may contain :ref:`modify-argument` nodes.

Typically when adding a function some code must be injected to provide the function
logic. This can be done using the :ref:`inject-code` node.

.. code-block:: xml

     <object-type>
         <add-function signature="..." return-type="..."
                       access="public | protected"
                       overload-number="number"
                       static="yes | no" classmethod="yes | no"
                       python-override ="yes | no"
                       since="..."
                       doc-signature="..."
                       deprecated = "true | false" />
     </object-type>

The ``return-type`` attribute defaults to *void*, the ``access`` to *public* and the ``static`` one to *no*.

The ``since`` attribute specifies the API version when this function was added.

The ``classmethod`` attribute specifies whether the function should be a Python class method.
It sets the METH_CLASS flag which means that ``PyTypeObject`` instead of an instance
``PyObject`` is passed as self, which needs to be handled in injected code.

For the *optional* attribute ``overload-number``, see :ref:`modify-function`.

Note that the label "static" in Qt's class documentation almost always means that a Python
``classmethod`` should be generated, because an object's class is always accessible from the
static C++ code, while Python needs the explicit "self" parameter that ``classmethod``
provides.

In order to create keyword argument supporting function parameters, enclose the specific
function parameter with a *@* in the `signature` field.

.. code-block:: xml

    <add-function signature="foo(int @parameter1@,float @parameter2@)">
        ...
    </add-function>

With keyword arguments, ``add-function`` makes it easy to specify a default argument
within the `signature` field

.. code-block:: xml

    <add-function signature="foo(int @parameter1@=1,float @parameter2@=2)">
        ...
    </add-function>

See :ref:`sequence-protocol` for adding the respective functions.

The *optional* attribute ``python-override`` indicates a special type
of added function, a python-override that will be generated into
the native wrapper (see :ref:`modifying-virtual-functions`).

The *optional* attribute ``doc-signature`` attribute can
be used to specify a signature (no return type, parameter type names
only) to retrieve the documentation from an existing C++ function.
This is useful when for example adding a function taking a Python
callable replacing a C++ function taking a C++ callable.

.. note:: In the case of `qdoc`_-generated documentation, functions
          marked as ``\\overload`` or property getter/setter functions
          do not have documentation.

The *optional* **deprecated** attribute can be used to mark the
function as deprecated.

.. _declare-function:

declare-function
^^^^^^^^^^^^^^^^

The ``declare-function`` node allows you to declare a function present in
the type and it is a child of an :ref:`object-type` or :ref:`value-type` nodes
if the function is supposed to be a method, or :ref:`namespace` and
:ref:`typesystem_details` if the function is supposed to be a function inside a
namespace or a global function. It may contain :ref:`modify-argument` nodes.

.. code-block:: xml

     <container-type>
         <declare-function signature="..." return-type="..." since="..."
                           allow-thread="true | auto | false"
                           exception-handling="off | auto-off | auto-on | on"
                           overload-number="number"
                           snake-case="yes | no | both"/>
     </container-type>

The ``return-type`` attribute defaults to *void*.

The ``since`` attribute specifies the API version when this function was
added.

For the *optional* attributes ``allow-thread``, ``exception-handling``,
``overload-number`` and ``snake-case``, see :ref:`modify-function`.

This is useful to make functions known to shiboken which its code parser
does not detect. For example, in Qt 6, the ``append()`` function of the
``QList<T>`` container takes an argument of ``parameter_type`` which is
specialized to ``T`` for simple types and ``const T &`` for complex types
by some template expression which the code parser cannot resolve.
In that case, the function can be declared with a simple signature:

.. code-block:: xml

     <container-type name="QList">
         <declare-function signature="append(T)"/>
     </container-type>

This tells shiboken a public function of that signature exists and
bindings will be created in specializations of ``QList``.


.. _add-pymethoddef:

add-pymethoddef
^^^^^^^^^^^^^^^

The ``add-pymethoddef`` element allows you to add a free function to
the ``PyMethodDef`` array of the type. No argument or result conversion
is generated, allowing for variadic functions and more flexible
arguments checking.

.. code-block:: xml

     <add-pymethoddef name="..." function="..." flags="..." doc="..."
                      signatures="..."/>

The ``name`` attribute specifies the name.

The ``function`` attribute specifies the implementation (a static function
of type ``PyCFunction``).

The ``flags`` attribute specifies the flags (typically ``METH_VARARGS``,
see `Common Object Structures`_).

The optional ``doc`` attribute specifies the documentation to be set to the
``ml_doc`` field.

The optional ``signatures`` attribute specifies a semicolon-separated list
of signatures of the function.

.. _Common Object Structures: https://docs.python.org/3/c-api/structures.html

.. _property-declare:

property
^^^^^^^^

The ``property`` element allows you to specify properties consisting of
a type and getter and setter functions.

It may appear as a child of a complex type such as :ref:`object-type` or
:ref:`value-type`.

If the PySide6 extension is not present, code will be generated using the
``PyGetSetDef`` struct, similar to what is generated for fields.

If the PySide6 extension is present, those properties complement the
properties obtained from the ``Q_PROPERTY`` macro in Qt-based code.
The properties will be handled in ``libpyside`` unless code generation
is forced.

.. code-block:: xml

    <property name="..." type="..." get="..." set="..."
              generate-getsetdef="yes | no" since="..."/>

The ``name`` attribute specifies the name of the property, the ``type``
attribute specifies the C++ type and the ``get`` attribute specifies the
name of the accessor function.

The optional ``set`` attribute specifies name of the setter function.

The optional ``generate-getsetdef`` attribute specifies whether to generate
code for if the PySide6 extension is present (indicating this property is not
handled by libpyside). It defaults to *no*.

The optional ``since`` attribute specifies the API version when this
property appears.

For a typical C++ class, like:

.. code-block:: c++

    class Test {
    public:
        int getValue() const;
        void setValue();
    };

``value`` can then be specified to be a property:

.. code-block:: xml

    <value-type name="Test">
        <property name="value" type="int" get="getValue" set="setValue"/>

With that, a more pythonic style can be used:

.. code-block:: python

    test = Test()
    test.value = 42

For Qt classes (with the PySide6 extension present), additional setters
and getters that do not appear as ``Q_PROPERTY``, can be specified to
be properties:

.. code-block:: xml

    <object-type name="QMainWindow">
        <property name="centralWidget" type="QWidget *"
                  get="centralWidget" set="setCentralWidget"/>

in addition to the normal properties of ``QMainWindow`` defined for
Qt Designer usage.

.. note:: In the *Qt* coding style, the property name typically conflicts
    with the getter name. It is recommended to exclude the getter from the
    wrapper generation using the ``remove`` function modification.

.. _configuration-element:

configuration
^^^^^^^^^^^^^

The ``configuration`` element allows you to generate a preprocessor
condition excluding a type depending on an expression into the module
header. This is specifically tailored to the
`Qt Feature system <https://doc.qt.io/qt-6/configure-options.html>`_ ,
but may also be used for similar systems.

It may appear as a child of a complex type such as :ref:`object-type` or
:ref:`value-type`.

.. code-block:: xml

    <configuration condition="..."/>

The ``condition`` attribute specifies the preprocessor condition.

This is an alternative way of omitting classes depending on some
configuration (see also option :ref:`drop-type-entries`) intended
for building several configurations from one generated source tree,
but still requires listing the correct source files in the
``CMakeLists.txt`` file.

.. _modifying-virtual-functions:

Modifying virtual functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Some C++ virtual functions are unsuitable for Python bindings:

.. code-block:: c

    virtual void getInt(int *result) const;

In that case, you would modify it to return the integer instead (or a tuple
in case of several out-parameters):

.. code-block:: c

    virtual int getInt() const;

In most cases, this can be achieved by specifying conversion rules for
arguments (see :ref:`conversionrule-on-arguments`, :ref:`modify-argument`)
for the Python wrapper as well as C++ wrapper code.

For cases where this is not possible, code implementing a Python override can
be generated for arbitrary signatures.

Taking the above example, to make it possible to reimplement the function in
Python with the modified signature, add a ``python-override`` function with
that signature, using an arbitrary name for disambiguation:

.. code-block:: xml

       <add-function signature="getIntPyOverride()"
                     return-type="int" python-override="true"/>

This causes a static function performing the call into Python for the override
to be generated into the native wrapper.

In the existing virtual function, inject a code snippet at the ``shell`` /
``override`` position which calls the newly added function. The first 2
arguments are the `Global interpreter lock handle` (``Shiboken::GilState``) and
the Python method determined by the override check (``PyObject *``). The
snippet then converts the arguments and return values and returns after that:

.. code-block:: xml

       <modify-function signature="getInt(int*)const">
           <inject-code class="shell" position="override">
           *result = getIntPyOverride(gil, pyOverride.object());
           return;
           </inject-code>
       </modify-function>


.. _`qdoc`: https://doc.qt.io/qt-6/qdoc-index.html
