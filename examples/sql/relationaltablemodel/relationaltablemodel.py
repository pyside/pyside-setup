
#############################################################################
##
## Copyright (C) 2021 The Qt Company Ltd.
## Contact: http://www.qt.io/licensing/
##
## This file is part of the Qt for Python examples of the Qt Toolkit.
##
## $QT_BEGIN_LICENSE:BSD$
## You may use this file under the terms of the BSD license as follows:
##
## "Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are
## met:
##   * Redistributions of source code must retain the above copyright
##     notice, this list of conditions and the following disclaimer.
##   * Redistributions in binary form must reproduce the above copyright
##     notice, this list of conditions and the following disclaimer in
##     the documentation and/or other materials provided with the
##     distribution.
##   * Neither the name of The Qt Company Ltd nor the names of its
##     contributors may be used to endorse or promote products derived
##     from this software without specific prior written permission.
##
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
## "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
## LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
## A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
## OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
## SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
## LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
## DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
## THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
## (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
## OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
##
## $QT_END_LICENSE$
##
#############################################################################

"""PySide6 port of the relationaltablemodel example from Qt v6.x"""

import sys

import connection

from PySide6.QtCore import QObject, Qt
from PySide6.QtSql import (QSqlQuery, QSqlRelation, QSqlRelationalDelegate,
                           QSqlRelationalTableModel)
from PySide6.QtWidgets import QApplication, QTableView


def initializeModel(model):

    model.setTable("population")
    model.setEditStrategy(QSqlRelationalTableModel.OnManualSubmit)
    model.setRelation(2, QSqlRelation("city", "id", "name"))
    model.setRelation(3, QSqlRelation("country", "id", "name"))
    model.setHeaderData(0, Qt.Horizontal, QObject().tr("ID"))

    model.setHeaderData(1, Qt.Horizontal, QObject().tr("Population"))
    model.setHeaderData(2, Qt.Horizontal, QObject().tr("City"))
    model.setHeaderData(3, Qt.Horizontal, QObject().tr("Country"))

    model.select()


def createView(title, model):

    table_view = QTableView()
    table_view.setModel(model)
    table_view.setItemDelegate(QSqlRelationalDelegate(table_view))
    table_view.setWindowTitle(title)

    return table_view


def createRelationalTables():

    query = QSqlQuery()

    query.exec("create table population(id int primary key, population int, city int, country int)")
    query.exec("insert into population values(1, '634293', 5000, 47)")
    query.exec("insert into population values(2, '1472000', 80000, 49)")
    query.exec("insert into population values(3, '1028000', 100, 1)")

    query.exec("create table city(id int, name varchar(20))")
    query.exec("insert into city values(100, 'San Jose')")
    query.exec("insert into city values(5000, 'Oslo')")
    query.exec("insert into city values(80000, 'Munich')")

    query.exec("create table country(id int, name varchar(20))")
    query.exec("insert into country values(1, 'USA')")
    query.exec("insert into country values(47, 'Norway')")
    query.exec("insert into country values(49, 'Germany')")


if __name__ == "__main__":
    app = QApplication(sys.argv)

    connection.createConnection()
    createRelationalTables()

    model = QSqlRelationalTableModel()

    initializeModel(model)

    title = "Relational Table Model"

    window = createView(title, model)
    window.resize(600, 200)
    window.show()

    sys.exit(app.exec())
