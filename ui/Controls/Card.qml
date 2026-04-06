/*!
    \file        Card.qml
    \brief       Implements the Card QML component for RAAD.
    \details     This file contains the Card user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls as QQC2

QQC2.Frame {
    // Compatibility-only properties used by existing layout code.
    property bool highlighted: false
    property bool subtle: false
    property bool elevated: false

    padding: 0
}
