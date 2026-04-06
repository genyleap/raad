/*!
    \file        Text.qml
    \brief       Implements the Text QML component for RAAD.
    \details     This file contains the Text user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick as T
import Raad

T.Text {
    font.family: FontSystem.getContentFontRegular.name
    horizontalAlignment: Text.AlignLeft
    wrapMode: Text.WordWrap
    font.pixelSize: Typography.t2
    textFormat: Text.AutoText
    color: Colors.textSecondary
    elide: if(AppGlobals.rtl == true) {
               Text.ElideLeft
           } else {
               Text.ElideRight
           }
}
