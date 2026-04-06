/*!
    \file        CheckBox.qml
    \brief       Implements the CheckBox QML component for RAAD.
    \details     This file contains the CheckBox user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick as QQ
import QtQuick.Templates as T
import QtQuick.Effects

import Raad

T.CheckBox {
    id: control

    implicitWidth: Math.max(22 + (contentItem.visible ? contentItem.implicitWidth + spacing : 0), 22)
    implicitHeight: Math.max(22, contentItem.implicitHeight)

    spacing: 10
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Typography.t2

    indicator: Rectangle {
        id: backgroundCheck
        implicitWidth: 22
        implicitHeight: 22
        x: 0
        y: (control.height - height) / 2
        radius: Metrics.innerRadius / 2
        anchors.left: parent.left

        color: {
            if (!control.enabled)
                return Colors.backgroundDeactivated
            if (control.checked)
                return Colors.backgroundItemActivated
            if (control.down)
                return Colors.backgroundFocused
            if (control.hovered)
                return Colors.backgroundActivated
            return Colors.backgroundItemActivated
        }


        border.width: 1
        border.color: control.checked ? Colors.secondry : Colors.borderActivated


        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }


        QQ.Text {
            anchors.centerIn: parent
            text: "\u2713"
            font.pixelSize: Typography.t2
            font.weight: Font.DemiBold
            color: Colors.secondry
            opacity: control.checked ? 1 : 0
            scale: control.checked ? 1.0 : 0.7

            Behavior on opacity {
                NumberAnimation
                {
                    duration: Animations.fast;
                    easing.type: Easing.InOutElastic;
                    easing.amplitude: 2.0;
                    easing.period: 1.5
                }
            }
            Behavior on scale {
                NumberAnimation
                {
                    duration: Animations.fast;
                    easing.type: Easing.InOutElastic;
                    easing.amplitude: 2.0;
                    easing.period: 1.5
                }
            }
        }

        Shadow {}
    }

    contentItem: QQ.Text {
        text: control.text
        font: control.font
        visible: text.length > 0
        leftPadding: control.indicator.width + control.spacing
        rightPadding: 0
        verticalAlignment: QQ.Text.AlignVCenter
        elide: QQ.Text.ElideRight
        color: control.enabled ? Colors.primary : Colors.textMuted
    }

    Rectangle {
        anchors.fill: control.indicator
        radius: 7
        color: "transparent"
        border.width: 2
        border.color: control.checked ? Colors.secondry : Colors.borderActivated
        opacity: control.visualFocus ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity {
            NumberAnimation { duration: 120;
                easing.type: Easing.InOutElastic;
                easing.amplitude: 2.0;
                easing.period: 1.5
            }
        }
    }
}
