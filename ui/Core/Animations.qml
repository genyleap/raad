/*!
    \file        Animations.qml
    \brief       Provides the Animations core QML definition for RAAD.
    \details     This file contains shared Animations values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

// Copyright (c) 2026 Genyleap
// Author: Kambiz Asadzadeh (compez.eth)
// License: https://github.com/genyleap/raad/blob/main/LICENSE.md

pragma Singleton
import QtQuick

QtObject {
    property int superfast: 64
    property int fast: 128
    property int normal: 256
    property int slow: 512
    property int veryslow: 1024
}
