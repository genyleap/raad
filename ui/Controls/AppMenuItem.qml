/*!
    \file        AppMenuItem.qml
    \brief       Implements the AppMenuItem QML component for RAAD.
    \details     This file contains the AppMenuItem user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Raad

MenuItem {
    id: control

    property string iconGlyph: ""

    implicitWidth: Math.max(220, rowContent.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 42
    padding: Metrics.padding
    contentItem: RowLayout {
        id: rowContent
        spacing: Metrics.padding

        Text {
            width: 22
            visible: control.iconGlyph.length > 0
            text: control.iconGlyph
            font.family: FontSystem.getAwesomeSolid.name
            font.pixelSize: Typography.t2
            color: control.enabled ? Colors.textSecondary : Colors.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            text: control.text
            font.pixelSize: Typography.t2
            font.weight: Font.Light
            color: control.enabled ? Colors.textPrimary : Colors.textMuted
            verticalAlignment: Text.AlignVCenter
        }

        Item { width: 1; height: 1 }

        Item { Layout.fillWidth: true; }
    }

    arrow: Text {
        anchors.fill: parent
        anchors.leftMargin: parent.width / 1.1
        visible: !!control.subMenu
        font.weight: Font.Light
        font.bold: false
        text: "\u203A"
        x: parent.width
        font.pixelSize: Typography.h2
        color: control.enabled ? Colors.textSecondary : Colors.textMuted
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    background: Rectangle {
        radius: Metrics.innerRadius
        border.width: control.enabled ? 1 : 0
        border.color: Colors.borderHovered
        color: control.highlighted ? Colors.backgroundItemHovered : "transparent"
        opacity: (control.hovered || control.highlighted) ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }
}
