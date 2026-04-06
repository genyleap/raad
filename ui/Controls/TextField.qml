/*!
    \file        TextField.qml
    \brief       Implements the TextField QML component for RAAD.
    \details     This file contains the TextField user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls as T

import Raad // Colors, Metrics

T.TextField {
    id: control

    implicitWidth: Math.max(220, implicitBackgroundWidth + leftInset + rightInset)
    implicitHeight: 40

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    selectByMouse: true

    // --- Padding tuned for optical vertical centering
    leftPadding: Metrics.padding * 2
    rightPadding: Metrics.padding * 2
    topPadding: Metrics.padding + 4
    bottomPadding: Metrics.padding + 4

    // Colors
    color: Colors.textPrimary
    placeholderTextColor: Colors.textMuted
    selectionColor: Colors.secondryBack
    selectedTextColor: Colors.textPrimary

    opacity: enabled ? 1.0 : 0.65

    background: Rectangle {
        radius: Metrics.outerRadius
        color: {
            if (!control.enabled) return Colors.backgroundItemDeactivated
            if (control.activeFocus) return Colors.backgroundItemFocused
            if (control.hovered) return Colors.backgroundItemHovered
            return Colors.backgroundItemActivated
        }

        border.width: 1
        border.color: {
            if (!control.enabled) return Colors.borderDeactivated
            if (control.activeFocus) return Colors.borderFocused
            if (control.hovered) return Colors.borderHovered
            return Colors.borderActivated
        }

        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }

        Shadow {}
    }

    // Focus ring (separate, smooth)
    Rectangle {
        anchors.fill: parent
        radius: Metrics.outerRadius
        color: "transparent"
        border.width: 1
        border.color: Colors.borderFocused
        opacity: control.activeFocus ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }

    // Cursor that matches theme (no sizing tricks)
    cursorDelegate: Rectangle {
        width: 1
        color: Colors.primary
        opacity: control.activeFocus ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }
}
