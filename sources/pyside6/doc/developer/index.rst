.. _developer-notes:

Developer Notes
===============

Developing |project| requires people to understand different processes
and steps that need to be taken into account when dealing with topics
related to modules, bindings, examples, and more.

Development Topics
------------------

.. toctree::
   :maxdepth: 2

   add_module.rst
   add_port_example.rst
   add_tool.rst
   maintenance.md
   pythonversions.md
   documentation.rst
   adapt_qt.rst
   extras.rst
   qtasyncio.rst
   signalmanager.md
   pythonlibraries.md
   freethreading.md

Library documentation
---------------------

- `libshiboken`_
- `libpyside`_
- `libpysideqml`_
- `libpysideremoteobjects`_

.. _libshiboken: ../shiboken6/libshiboken/libshiboken-module.html
.. _libpyside: ../pyside6/libpyside/libpyside-module.html
.. _libpysideqml: ../pyside6/libpysideqml/libpysideqml-module.html
.. _libpysideremoteobjects: ../pyside6/libpysideremoteobjects/libpysideremoteobjects-module.html


Implementation details
----------------------

Complementary, here you can find the reasoning and motivation for the
many features and implementation details that the project has:

.. toctree::
   :maxdepth: 2

   enumfeatures_doc.rst
   limited_api.rst
   signature_doc.rst
   mypy-correctness.rst
   feature-motivation.rst
   remoteobjects.md
   fix_type_hints.rst
   embed.rst
