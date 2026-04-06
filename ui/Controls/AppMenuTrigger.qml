/*!
    \file        AppMenuTrigger.qml
    \brief       Implements the AppMenuTrigger QML component for RAAD.
    \details     This file contains the AppMenuTrigger user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick

import Raad // Colors

Item {
    id: control

    property string text: ""
    property var menu: null
    readonly property bool active: mouseArea.containsMouse || (menu && menu.visible)

    implicitWidth: Math.max(52, titleLabel.implicitWidth + 12)
    implicitHeight: 30

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: control.active ? Colors.backgroundItemActivated : "transparent"
    }

    Text {
        id: titleLabel
        anchors.centerIn: parent
        text: control.text
        font.pixelSize: Typography.t2
        font.weight: Font.Light
        color: control.active ? Colors.textPrimary : Colors.textSecondary
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (!control.menu)
                return
            control.menu.popup(control, 0, control.height + 6)
        }
    }
}
