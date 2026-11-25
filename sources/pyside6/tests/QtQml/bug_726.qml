// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick 2.0
import test.ProxyObject 1.0

Rectangle {
    id: page

    width: 500; height: 200
    color: "lightgray"
    required property ProxyObject proxy

    Rectangle {
        id: button
        width: 150; height: 40
        color: "darkgray"
        anchors.horizontalCenter: page.horizontalCenter
        y: 120
        MouseArea {
            id: buttonMouseArea
            objectName: "buttonMouseArea"
            anchors.fill: parent
            onEntered: {
                    let obj = proxy.getObject()
                    proxy.receivedObject(obj?.objectName ?? "None")
            }
        }
        Text {
            id: buttonText
                text: "Press me!"
            anchors.horizontalCenter: button.horizontalCenter
            anchors.verticalCenter: button.verticalCenter
            font.pointSize: 16;
        }
    }
}
