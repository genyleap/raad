/*!
    \file        CommandActionButton.qml
    \brief       Implements the CommandActionButton QML component for RAAD.
    \details     This file contains the CommandActionButton user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Templates as T

import Raad

T.Button {
    id: control

    property string iconGlyph: ""

    implicitWidth: 64
    implicitHeight: 64
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: Metrics.innerRadius
        color: {
            if (!control.enabled) return "transparent"
            if (control.down) return Colors.backgroundFocused
            if (control.hovered) return Colors.backgroundItemHovered
            return "transparent"
        }
        border.width: (control.hovered || control.visualFocus) ? 1 : 0
        border.color: control.visualFocus ? Colors.borderFocused : Colors.borderHovered
        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }

    }

    contentItem: Item {
        opacity: control.enabled ? 1.0 : 0.45

        Column {
            anchors.centerIn: parent
            spacing: 2

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: control.iconGlyph
                visible: text.length > 0
                font.family: FontSystem.getAwesomeLight.name
                font.pixelSize: 22
                font.weight: Font.Light
                color: Colors.accentPrimary
                horizontalAlignment: Text.AlignHCenter
                styleColor: Colors.accent
                style: Text.Sunken
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: control.text
                styleColor: Colors.accent
                style: Text.Sunken
                color: Colors.accentPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
