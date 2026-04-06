/*!
    \file        Typography.qml
    \brief       Provides the Typography core QML definition for RAAD.
    \details     This file contains shared Typography values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

pragma Singleton
import QtQuick

QtObject {
    readonly property int       t1 : 16
    readonly property int       t2 : 14
    readonly property int       t3 : 12
    readonly property int       t4 : 10
    readonly property int       t5 : 8
    readonly property int       t6 : 6

    readonly property int       h1 : 32
    readonly property int       h2 : 24
    readonly property double    h3 : 18.72
    readonly property int       h4 : 16
    readonly property double    h5 : 13.28
    readonly property double    h6 : 10.72

    readonly property int       display1 : 96
    readonly property int       display2 : 88
    readonly property int       display3 : 72
    readonly property int       display4 : 56
    readonly property int       display5 : 40
    readonly property int       display6 : 24

    readonly property int       paragraph : 14
}
