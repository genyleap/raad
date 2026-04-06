/*!
    \file        AppMenuBar.qml
    \brief       Implements the AppMenuBar QML component for RAAD.
    \details     This file contains the AppMenuBar user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls

import "." as Kit

MenuBar {
    id: control

    implicitHeight: 34
    spacing: 16

    background: Item { }

    delegate: Kit.AppMenuBarItem { }
}
