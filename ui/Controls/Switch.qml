/*!
    \file        Switch.qml
    \brief       Implements the Switch QML component for RAAD.
    \details     This file contains the Switch user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Effects

import Raad

T.Switch {
    id: control
    property string title
    property string description
    property string setIcon
    property bool animation : false

    focus: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    text: title
    font.family: FontSystem.getContentFont.name
    font.pixelSize: Typography.t2

    indicator: Rectangle {
        id: backgroundSwitch
        implicitWidth: 42
        implicitHeight: 22
        anchors.left: parent.left
        radius: Colors.radius
        anchors.verticalCenter: parent.verticalCenter
        border.width:  1
        border.color: control.activeFocus ? Colors.secondry : control.checked ? Colors.secondry : Colors.borderActivated

        color: {
            if (!control.enabled) return Colors.backgroundItemDeactivated
            if (control.checked) return Colors.secondry
            if (control.hovered)  return Colors.backgroundItemHovered
            return Colors.backgroundItemActivated
        }

        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }

        Rectangle {
            id: backgroundSwitchFocus
            anchors.fill: parent
            anchors.margins: -3
            radius: Colors.radius
            anchors.verticalCenter: parent.verticalCenter
            color: "transparent"
            border.width:  control.activeFocus ? 4 : 0
            border.color: control.activeFocus ? Qt.lighter(Colors.primaryBack) : "transparent"
            z: -1

        }

        Rectangle {
            id: rectTwo
            x: control.checked ? parent.width / 1.7 : 5
            width: 13
            height: 13
            radius: width
            anchors.verticalCenter: parent.verticalCenter
            color: control.checked ? Colors.staticPrimary : Colors.primary

            Behavior on x {
                enabled: true
                NumberAnimation {
                    duration: Animations.normal
                    easing.type: Easing.InOutElastic;
                    easing.amplitude: 2.0;
                    easing.period: 1.5
                }
            }

            Behavior on color { ColorAnimation { duration: 200} }
        }

        Shadow {}
    }

    contentItem:
        Text {
        text: control.text
        font: control.font
        fontSizeMode: Text.Fit
        leftPadding: control.indicator.width + control.spacing
        rightPadding: appRootObjects.isLeftToRight ? 0 : 48
        opacity: enabled ? 1.0 : 0.3
        color: Colors.primary
        wrapMode: Text.WordWrap
        Behavior on color { ColorAnimation {
                duration: Animations.normal
            }
        }
    }
}
