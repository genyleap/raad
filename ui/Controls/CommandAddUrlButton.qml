/*!
    \file        CommandAddUrlButton.qml
    \brief       Implements the CommandAddUrlButton QML component for RAAD.
    \details     This file contains the CommandAddUrlButton user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

import Raad // Colors, FontSystem

T.Button {
    id: control

    property string iconGlyph: "\uf0c1"
    property string badgeGlyph: "\uf093"

    implicitWidth: 308
    implicitHeight: 56
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: Metrics.innerRadius
        border.width: 1
        border.color: control.visualFocus ? Colors.borderFocused : Colors.borderActivated
        color: Colors.backgroundActivated
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Colors.backgroundItemActivated
            opacity: control.down ? 0.35 : (control.hovered ? 0.18 : 0.0)
            Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
            Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
            Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        }

        Shadow {}
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: Metrics.outerRadius

        Text {
            text: control.iconGlyph
            font.family: FontSystem.getAwesomeSolid.name
            font.pixelSize: Typography.h3
            color: Colors.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            text: control.text
            color: Colors.textPrimary
            font.pixelSize: Typography.h5
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            radius: Metrics.innerRadius / 1.5
            border.width: 1
            border.color: Colors.borderActivated
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.45; color: Colors.backgroundActivated }
                GradientStop { position: 1.0; color: Colors.backgroundItemActivated }
            }

            Text {
                anchors.centerIn: parent
                text: control.badgeGlyph
                font.family: FontSystem.getAwesomeSolid.name
                font.pixelSize: Typography.h3
                color: Colors.primary
            }
        }
    }
}
