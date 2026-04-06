/*!
    \file        SidebarTreeItem.qml
    \brief       Implements the SidebarTreeItem QML component for RAAD.
    \details     This file contains the SidebarTreeItem user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

import Raad // Colors, FontSystem

T.AbstractButton {
    id: control

    property string iconGlyph: ""
    property bool expandable: false
    property bool expanded: false
    property bool selected: false
    property bool child: false

    implicitWidth: 200
    implicitHeight: child ? 44 : 48
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: Metrics.innerRadius
        color: {
            if (control.child && control.selected) return Colors.secondryBack
            if (control.pressed) return Colors.backgroundFocused
            if (control.hovered) return Colors.backgroundItemHovered
            return "transparent"
        }

        Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: control.child ? 22 : 12
        anchors.rightMargin: 10
        spacing: 9

        Text {
            Layout.preferredWidth: 22
            text: control.iconGlyph
            visible: text.length > 0
            font.family: FontSystem.getAwesomeLight.name
            font.weight: Font.Light
            font.pixelSize: control.child ? Typography.t2 : Typography.t1
            color: control.selected ? Colors.textPrimary : Colors.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            Layout.fillWidth: true
            text: control.text
            font.pixelSize: control.child ? Typography.t2 : Typography.t1
            font.weight: control.child ? Font.Light : Font.Medium
            color: control.selected ? Colors.textPrimary : Colors.textSecondary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            visible: control.expandable
            text: "\uf078"
            font.family: FontSystem.getAwesomeSolid.name
            font.pixelSize: Typography.t2
            color: Colors.textMuted
            verticalAlignment: Text.AlignVCenter
            rotation: control.expanded ? 180 : 0
            transformOrigin: Item.Center
            Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }
}
