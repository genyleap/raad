/*!
    \file        Shadow.qml
    \brief       Provides the Shadow core QML definition for RAAD.
    \details     This file contains shared Shadow values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick.Effects
import QtQuick

RectangularShadow {
    anchors.fill: parent
    offset.x: 5
    offset.y: 5
    radius: parent.radius
    blur: 16
    spread: -6
    color: Colors.lightShadow
    z: -1
}
