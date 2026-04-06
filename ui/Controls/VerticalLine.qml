/*!
    \file        VerticalLine.qml
    \brief       Implements the VerticalLine QML component for RAAD.
    \details     This file contains the VerticalLine user interface component used by the RAAD desktop application.

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

    Layout.fillHeight: true;

    Item { width: 1; }

    Rectangle {

        property int heightSize : parent.height
        property int lineSize : 1

        Layout.fillHeight: true;

        height: heightSize
        width: lineSize
        color: Colors.lineBorderActivated
    }

    Item { width: 1; }

}
