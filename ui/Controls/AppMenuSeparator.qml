/*!
    \file        AppMenuSeparator.qml
    \brief       Implements the AppMenuSeparator QML component for RAAD.
    \details     This file contains the AppMenuSeparator user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls

import Raad // Colors

MenuSeparator {
    id: control

    implicitWidth: 220
    implicitHeight: 9
    padding: 0

    contentItem: Rectangle {
        x: 8
        width: parent.width - 16
        y: (parent.height - 1) / 2
        height: 1
        color: Colors.lineBorderActivated
        opacity: 0.5
    }
}
