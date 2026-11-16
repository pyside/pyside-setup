# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import os
import subprocess
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QUrl
from PySide6.QtGui import QColor, QImage, QTextDocument

RESOURCE_URL = "test://resource"

# The default provider is a global in Qt, destroyed after the interpreter has
# been finalized. Releasing the callable must not touch the dead interpreter.
EXIT_SCRIPT = """
from PySide6.QtGui import QTextDocument
QTextDocument.setDefaultResourceProvider(lambda url: "value")
"""


class ResourceProviderTest(unittest.TestCase):
    """PYSIDE-3518: QTextDocument resource provider functions."""

    def tearDown(self):
        QTextDocument.setDefaultResourceProvider(None)

    def testDocumentProvider(self):
        doc = QTextDocument()
        self.assertIsNone(doc.resourceProvider())

        urls = []

        def provider(url):
            urls.append(url)
            return "hello"

        doc.setResourceProvider(provider)
        self.assertIs(doc.resourceProvider(), provider)
        result = doc.resource(QTextDocument.ResourceType.UnknownResource,
                              QUrl(RESOURCE_URL))
        self.assertEqual(result, "hello")
        self.assertEqual(urls, [QUrl(RESOURCE_URL)])

    def testDefaultProvider(self):
        self.assertIsNone(QTextDocument.defaultResourceProvider())

        def provider(url):
            return url.toString()

        QTextDocument.setDefaultResourceProvider(provider)
        self.assertIs(QTextDocument.defaultResourceProvider(), provider)
        doc = QTextDocument()
        result = doc.resource(QTextDocument.ResourceType.UnknownResource,
                              QUrl(RESOURCE_URL))
        self.assertEqual(result, RESOURCE_URL)

    def testImageResource(self):
        image = QImage(4, 4, QImage.Format.Format_ARGB32)
        image.fill(QColor("red"))

        QTextDocument.setDefaultResourceProvider(lambda url: image)
        doc = QTextDocument()
        result = doc.resource(QTextDocument.ResourceType.ImageResource,
                              QUrl(RESOURCE_URL))
        self.assertIsInstance(result, QImage)
        self.assertEqual(result.pixelColor(0, 0), QColor("red"))

    def testClearProvider(self):
        doc = QTextDocument()
        doc.setResourceProvider(lambda url: "document")
        doc.setResourceProvider(None)
        self.assertIsNone(doc.resourceProvider())

        QTextDocument.setDefaultResourceProvider(lambda url: "default")
        QTextDocument.setDefaultResourceProvider(None)
        self.assertIsNone(QTextDocument.defaultResourceProvider())

    def testInvalidProvider(self):
        doc = QTextDocument()
        with self.assertRaises(TypeError):
            doc.setResourceProvider(5)
        with self.assertRaises(TypeError):
            QTextDocument.setDefaultResourceProvider("not callable")
        self.assertIsNone(doc.resourceProvider())
        self.assertIsNone(QTextDocument.defaultResourceProvider())

    def testExceptionInProvider(self):
        def provider(url):
            raise RuntimeError("expected")

        doc = QTextDocument()
        doc.setResourceProvider(provider)
        with self.assertRaises(RuntimeError):
            doc.resource(QTextDocument.ResourceType.UnknownResource,
                         QUrl(RESOURCE_URL))

    def testProviderReleased(self):
        class Provider:
            def __call__(self, url):
                return "value"

        provider = Provider()
        refcount = sys.getrefcount(provider)
        doc = QTextDocument()
        doc.setResourceProvider(provider)
        self.assertGreater(sys.getrefcount(provider), refcount)
        doc.setResourceProvider(None)
        self.assertEqual(sys.getrefcount(provider), refcount)

    def testDefaultProviderAtExit(self):
        env = dict(os.environ, PYTHONPATH=os.pathsep.join(sys.path))
        result = subprocess.run([sys.executable, "-c", EXIT_SCRIPT], env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
