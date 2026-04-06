/*!
    \file        AppMenuBarItem.qml
    \brief       Implements the AppMenuBarItem QML component for RAAD.
    \details     This file contains the AppMenuBarItem user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls

import Raad // Colors

MenuBarItem {
    id: control

    implicitHeight: 34
    implicitWidth: Math.max(66, labelItem.implicitWidth + 12)
    padding: 0

    contentItem: Text {
        id: labelItem
        text: control.text
        font.pixelSize: 14
        font.weight: Font.Medium
        color: control.highlighted ? Colors.textPrimary : Colors.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 8
        color: control.highlighted ? Colors.backgroundItemActivated : "transparent"
    }
}
