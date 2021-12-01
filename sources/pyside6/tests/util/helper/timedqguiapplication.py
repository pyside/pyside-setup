#############################################################################
##
## Copyright (C) 2021 The Qt Company Ltd.
## Contact: https://www.qt.io/licensing/
##
## This file is part of the test suite of Qt for Python.
##
## $QT_BEGIN_LICENSE:GPL-EXCEPT$
## Commercial License Usage
## Licensees holding valid commercial Qt licenses may use this file in
## accordance with the commercial license agreement provided with the
## Software or, alternatively, in accordance with the terms contained in
## a written agreement between you and The Qt Company. For licensing terms
## and conditions see https://www.qt.io/terms-conditions. For further
## information use the contact form at https://www.qt.io/contact-us.
##
## GNU General Public License Usage
## Alternatively, this file may be used under the terms of the GNU
## General Public License version 3 as published by the Free Software
## Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
## included in the packaging of this file. Please review the following
## information to ensure the GNU General Public License requirements will
## be met: https://www.gnu.org/licenses/gpl-3.0.html.
##
## $QT_END_LICENSE$
##
#############################################################################

'''Helper classes and functions'''

import gc
import unittest

from PySide6.QtCore import QTimer
from PySide6.QtGui import QGuiApplication


class TimedQGuiApplication(unittest.TestCase):
    '''Helper class with timed QGuiApplication exec loop'''

    def setUp(self, timeout=100):
        '''Sets up this Application.

        timeout - timeout in millisseconds'''
        self.app = QGuiApplication.instance() or QGuiApplication([])
        QTimer.singleShot(timeout, self.app.quit)

    def tearDown(self):
        '''Delete resources'''
        del self.app
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
