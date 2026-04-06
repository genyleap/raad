/*!
    \file        HorizontalLine.qml
    \brief       Implements the HorizontalLine QML component for RAAD.
    \details     This file contains the HorizontalLine user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

// Copyright (C) 2022 The Genyleap.
// Copyright (C) 2022 Kambiz Asadzadeh
// SPDX-License-Identifier: LGPL-3.0-only

import QtQuick
import QtQuick.Layouts

import Raad

ColumnLayout {

    Layout.fillWidth: true;

    property string setColor : Colors.lineBorderActivated

    Rectangle {

        property int widthSize : parent.width
        property int lineSize : 1

        Layout.fillWidth: true;

        width: widthSize
        height: lineSize
        color: setColor
    }

}
