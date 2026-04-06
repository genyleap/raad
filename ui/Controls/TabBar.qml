/*!
    \file        TabBar.qml
    \brief       Implements the TabBar QML component for RAAD.
    \details     This file contains the TabBar user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

import Raad // Colors, Metrics

T.TabBar {
    id: control

    implicitHeight: 46
    spacing: 4
    leftPadding: 4
    rightPadding: 4
    topPadding: 4
    bottomPadding: 4

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex

        orientation: ListView.Horizontal
        spacing: control.spacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 140
        interactive: contentWidth > width

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    background: Rectangle {
        radius: Metrics.outerRadius
        color: Colors.backgroundActivated
        border.width: 1
        border.color: Colors.borderActivated
    }
}
