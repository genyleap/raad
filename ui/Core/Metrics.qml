/*!
    \file        Metrics.qml
    \brief       Provides the Metrics core QML definition for RAAD.
    \details     This file contains shared Metrics values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

pragma Singleton
import QtQuick

QtObject {
    property int spacing: 8
    property int padding: 8
    property int margins: 8
    property int cornerRadius: 24
    property int outerRadius: 18
    property int innerRadius: 14
    property int shadowOffset: 6
}
