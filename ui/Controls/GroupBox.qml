/*!
    \file        GroupBox.qml
    \brief       Implements the GroupBox QML component for RAAD.
    \details     This file contains the GroupBox user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

import Raad

T.GroupBox {
    id: control

    // Space reserved for the title above the card
    readonly property int _titleH: (control.title && control.title.length > 0) ? 26 : 0
    property bool hasBorder: true
    property bool isSelected : false
    property bool hasShadow : false
    property string statusLevel : "Active"

    // Card padding
    leftPadding: Metrics.padding
    rightPadding: Metrics.padding
    topPadding: Metrics.padding + _titleH
    bottomPadding: Metrics.padding
    spacing: Metrics.padding
    property bool focusable : false

    function setColor(){
        if(control.statusLevel == "Done") {
           return Colors.successBack;
        } else if(control.statusLevel == "Paused") {
            return Colors.warningBack;
        } else if(control.statusLevel == "Error") {
            return Colors.errorBack;
        } else if(control.statusLevel == "Active") {
            return Colors.primaryBack;
        }
    }

    // -------- Title above (outside) --------
    label: Text {
        visible: control.title && control.title.length > 0
        text: control.title

        x: Metrics.padding * 2.5
        y: -Metrics.padding / 2.0

        height: control._titleH
        verticalAlignment: Text.AlignVCenter

        font.pixelSize: 16
        font.weight: Font.DemiBold
        color: Colors.textPrimary
        elide: Text.ElideRight

        // keep it inside the group width
        width: Math.max(0, control.width - Metrics.padding * 2)
    }


    // -------- Card background --------
    background: Rectangle {
        x: 0
        y: control._titleH
        width: control.width
        height: control.height - control._titleH

        radius: Metrics.outerRadius
        antialiasing: true

        color: control.isSelected ? Qt.lighter(setColor(), 1.1) : Colors.pagespaceActivated
        border.width: control.hasBorder ? 1.0 : 0.0
        border.color: control.isSelected ? Colors.primaryBack : Colors.lineBorderActivated

        Shadow {}
    }

    focusPolicy: Qt.StrongFocus

    // -------- Focus ring around the CARD only --------
    Rectangle {
        x: -8
        y: -10
        width: control.width
        height: control.height - control._titleH

        radius: Metrics.outerRadius
        color: "transparent"
        border.width: 2
        border.color: Colors.borderFocused

        opacity: control.activeFocus ? 1.0 : 0.0
        visible: control.focusable
        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }
}
