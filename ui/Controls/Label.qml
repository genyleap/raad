/*!
    \file        Label.qml
    \brief       Implements the Label QML component for RAAD.
    \details     This file contains the Label user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

import Raad // Colors, Metrics

T.Label {
    id: control

    // Spacing
    padding: 0

    font.family: FontSystem.getContentFont.name
    // Typography
    font.pixelSize: Typography.t3
    font.weight: Font.Normal

    // Text behavior
    wrapMode: Text.NoWrap
    elide: Text.ElideRight
    horizontalAlignment: Text.AlignLeft
    verticalAlignment: Text.AlignVCenter

    renderType: Label.QtRendering
    renderTypeQuality: Label.VeryHighRenderTypeQuality

    textFormat: Label.MarkdownText


    // Variants
    // 0: primary, 1: secondary, 2: muted, 3: accent, 4: success, 5: warning, 6: error
    property int role: 0

    color: {
        if (!control.enabled) return Colors.textMuted
        switch (control.role) {
        case 1: return Colors.textPrimary
        case 2: return Colors.textMuted
        case 3: return Colors.textAccent
        case 4: return Colors.textSuccess
        case 5: return Colors.textWarning
        case 6: return Colors.textError
        default: return Colors.textSecondary
        }
    }

    Behavior on color {
        ColorAnimation { duration: 160; easing.type: Easing.OutCubic }
    }
}
