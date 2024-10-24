.. _tutorial_signals_and_slots:


Signals and Slots
=================

Due to the nature of Qt, :class:`~PySide6.QtCore.QObject`\s
require a way to communicate, and that's the reason for this mechanism to
be a **central feature of Qt**.

In simple terms, you can understand **Signal and Slots** in the same way you
interact with the lights in your house. When you move the light switch
(signal) you get a result which may be that your light bulbs are switched
on/off (slot).

While developing interfaces, you can get a real example by the effect of
clicking a button: the 'click' will be the signal, and the slot will be what
happens when that button is clicked, like closing a window, saving a document,
etc.

.. note::
    If you have experience with other frameworks or toolkits, it's likely
    that you read a concept called 'callback'. Leaving the implementation
    details aside, a callback will be related to a notification function,
    passing a pointer to a function in case it's required due to the events
    that happen in your program. This approach might sound similar, but
    there are essential differences that make it an unintuitive approach,
    like ensuring the type correctness of callback arguments, and some others.

All classes that inherit from :class:`~PySide6.QtCore.QObject` or one of its
subclasses, like :class:`~PySide6.QtWidgets.QWidget`, can contain signals and
slots. **Signals are emitted by objects**
when they change their state in a way that may be interesting to other objects.
This is all the object does to communicate. It does not know or care whether
anything is receiving the signals it emits. This is true information
encapsulation, and ensures that the object can be used as a software component.

**Slots can be used for receiving signals**, but they are also normal member
functions. Just as an object does not know if anything receives its signals,
a slot does not know if it has any signals connected to it. This ensures that
truly independent components can be created with Qt.

You can connect as many signals as you want to a single slot, and a signal can
be connected to as many slots as you need. It is even possible to connect
a signal directly to another signal. (This will emit the second signal
immediately whenever the first is emitted.)

Qt's widgets have many predefined signals and slots. For example,
:class:`~PySide6.QtWidgets.QAbstractButton` (base class of buttons in Qt)
has a ``clicked()`` signal and :class:`~PySide6.QtWidgets.QLineEdit`
(single line input field) has a slot named ``clear()``.
So, a text input field with a button to clear the text
could be implemented by placing a :class:`~PySide6.QtWidgets.QToolButton`
to the right of the ``QLineEdit`` and connecting its ``clicked()`` signal to the slot
``clear()``. This is done using the :meth:`~PySide6.QtCore.Signal.connect`
method of the signal:

.. code-block:: python

    button = QToolButton()
    line_edit = QLineEdit()
    button.clicked.connect(line_edit.clear)

:meth:`~PySide6.QtCore.Signal.connect` returns a
:class:`~PySide6.QtCore.QMetaObject.Connection` object, which can be
used with the :meth:`~PySide6.QtCore.Signal.disconnect` method to sever
the connection.

Signals can also be connected to free functions:

.. code-block:: python

    import sys
    from PySide6.QtWidgets import QApplication, QPushButton


    def function():
        print("The 'function' has been called!")

    app = QApplication()
    button = QPushButton("Call function")
    button.clicked.connect(function)
    button.show()
    sys.exit(app.exec())

Connections can be spelled out in code or, for widget forms,
designed in the
`Signal-Slot Editor <https://doc.qt.io/qt-6/designer-connection-mode.html>`_
of *Qt Widgets Designer*.

The :meth:`~PySide6.QtCore.Signal.connect` function takes an optional parameter
of :class:`~PySide6.QtCore.Qt.ConnectionType` that specifies the behavior
with regards to threads and event loops.

The Signal Class
----------------

When writing classes in Python, signals are declared as class level
variables of the class :class:`~PySide6.QtCore.Signal`.
A :class:`~PySide6.QtWidgets.QWidget`-based button that emits a
``clicked()`` signal could look as follows:

.. code-block:: python

    from PySide6.QtCore import Qt, Signal
    from PySide6.QtWidgets import QWidget

    class Button(QWidget):

        clicked = Signal(Qt.MouseButton)

        ...

        def mousePressEvent(self, event):
            self.clicked.emit(event.button())

The constructor of ``Signal`` takes a tuple or a list of Python types
and C types:

.. code-block:: python

    signal1 = Signal(int)  # Python types
    signal2 = Signal(QUrl)  # Qt Types
    signal3 = Signal(int, str, int)  # more than one type
    signal4 = Signal((float,), (QDate,))  # optional types

In addition to that, it can receive also a named argument ``name`` that defines
the signal name. If nothing is passed, the new signal will have the same name
as the variable that it is being assigned to.

.. code-block:: python

    # TODO
    signal5 = Signal(int, name='rangeChanged')
    # ...
    rangeChanged.emit(...)

Another useful option of ``Signal`` is the arguments name,
useful for QML applications to refer to the emitted values by name:

.. code-block:: python

    sumResult = Signal(int, arguments=['sum'])

.. code-block:: javascript

    Connections {
        target: ...
        function onSumResult(sum) {
            // do something with 'sum'
        }


.. _slot-decorator:

The Slot Class
--------------

Slots in QObject-derived classes should be indicated by the decorator
:deco:`~PySide6.QtCore.Slot`. Again, to define a signature just pass the types
similar to the :class:`~PySide6.QtCore.Signal` class.

.. code-block:: python

    @Slot(str)
    def slot_function(self, s):
        ...


``Slot()`` also accepts a ``name`` and a ``result`` keyword.
The ``result`` keyword defines the type that will be returned and can be a C or
Python type. The ``name`` keyword behaves the same way as in ``Signal()``. If
nothing is passed as name then the new slot will have the same name as the
function that is being decorated.

We recommend marking all methods used by signal connections with a
:deco:`~PySide6.QtCore.Slot` decorator. Not doing causes run-time overhead
due to the method being added to the ``QMetaObject`` when creating the connection.
This is particularly important for ``QObject`` classes registered with QML, where
missing decorators can introduce bugs.

Missing decorators can be diagnosed by setting activating warnings of the
logging category ``qt.pyside.libpyside``; for example by setting the
environment variable:

.. code-block:: bash

    export QT_LOGGING_RULES="qt.pyside.libpyside.warning=true"

.. _overloading-signals-and-slots:

Overloading Signals and Slots with Different Types
--------------------------------------------------

It is actually possible to use signals and slots of the same name with different
parameter type lists. This is legacy from Qt 5 and not recommended for new code.
In Qt 6, signals have distinct names for different types.

The following example uses two handlers for a Signal and a Slot to showcase
the different functionality.

.. code-block:: python

    import sys
    from PySide6.QtWidgets import QApplication, QPushButton
    from PySide6.QtCore import QObject, Signal, Slot


    class Communicate(QObject):
        # create two new signals on the fly: one will handle
        # int type, the other will handle strings
        speak = Signal((int,), (str,))

        def __init__(self, parent=None):
            super().__init__(parent)

            self.speak[int].connect(self.say_something)
            self.speak[str].connect(self.say_something)

        # define a new slot that receives a C 'int' or a 'str'
        # and has 'say_something' as its name
        @Slot(int)
        @Slot(str)
        def say_something(self, arg):
            if isinstance(arg, int):
                print("This is a number:", arg)
            elif isinstance(arg, str):
                print("This is a string:", arg)

    if __name__ == "__main__":
        app = QApplication(sys.argv)
        someone = Communicate()

        # emit 'speak' signal with different arguments.
        # we have to specify the str as int is the default
        someone.speak.emit(10)
        someone.speak[str].emit("Hello everybody!")


.. _signals-and-slots-strings:

Specifying Signals and Slots by Method Signature Strings
--------------------------------------------------------


Signals and slots can also be specified as C++ method signature
strings passed through the ``SIGNAL()`` and/or ``SLOT()`` functions:

.. code-block:: python

    from PySide6.QtCore import SIGNAL, SLOT

    button.connect(SIGNAL("clicked(Qt::MouseButton)"),
                   action_handler, SLOT("action1(Qt::MouseButton)"))

This is not normally recommended; it is only needed
for a few cases where signals are only accessible via ``QMetaObject``
(``QAxObject``, ``QAxWidget``, ``QDBusInterface`` or ``QWizardPage::registerField()``):

.. code-block:: python

    wizard.registerField("text", line_edit, "text",
                         SIGNAL("textChanged(QString)"))

The signature strings can be found by querying ``QMetaMethod.methodSignature()``
when introspecting ``QMetaObject``:

.. code-block:: python

    mo = widget.metaObject()
    for m in range(mo.methodOffset(), mo.methodCount()):
        print(mo.method(m).methodSignature())

Slots should be decorated using :deco:`~PySide6.QtCore.Slot`.
