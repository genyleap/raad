/*!
    \file        TabButton.qml
    \brief       Implements the TabButton QML component for RAAD.
    \details     This file contains the TabButton user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick as QQ
import QtQuick.Templates as T

import Raad // Colors, Metrics

T.TabButton {
    id: control

    implicitWidth: 128
    implicitHeight: 38
    padding: Metrics.padding
    hoverEnabled: true

    background: Rectangle {
        radius: Metrics.innerRadius
        color: {
            if (!control.enabled)
                return Colors.backgroundDeactivated
            if (control.checked)
                return Colors.backgroundItemFocused
            if (control.down)
                return Colors.backgroundFocused
            if (control.hovered)
                return Colors.backgroundHovered
            return "transparent"
        }

        border.width: 0
        border.color: Colors.borderActivated

        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }



    }

    contentItem: QQ.Text {
        text: control.text
        horizontalAlignment: QQ.Text.AlignHCenter
        verticalAlignment: QQ.Text.AlignVCenter
        elide: QQ.Text.ElideRight
        font.pixelSize: 14
        font.weight: control.checked ? Font.DemiBold : Font.Medium
        color: control.enabled
               ? (control.checked ? Colors.textPrimary : Colors.textSecondary)
               : Colors.textMuted
    }
}
