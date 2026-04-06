/*!
    \file        AppMenu.qml
    \brief       Implements the AppMenu QML component for RAAD.
    \details     This file contains the AppMenu user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import Raad // Colors
import "." as Kit

Menu {
    id: control

    implicitWidth: 292
    topPadding: Metrics.padding
    bottomPadding: Metrics.padding
    leftPadding: Metrics.padding
    rightPadding: Metrics.padding
    overlap: 2

    delegate: Kit.AppMenuItem { }

    background: Rectangle {
        id: appMenuBack
        radius: Metrics.outerRadius
        color: control.enabled ? Colors.backgroundItemActivated : Colors.backgroundItemDeactivated
        layer.enabled: true
        layer.effect: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: Metrics.outerRadius
                border.width: 1
                border.color: Colors.borderActivated
                color: "transparent"
            }

            RectangularShadow {
                anchors.fill: parent
                offset.x: -10
                offset.y: -5
                radius: appMenuBack.radius
                blur: 64
                spread: 3
                color: Colors.lightShadow
                z: -1
            }

            MultiEffect {
                source: appMenuBack
                anchors.fill: parent
                autoPaddingEnabled: false
                blurEnabled: true
                blurMax: 32
                blur: 1.0
                maskEnabled: true
                maskSource: appMenuBack
            }
        }
    }
}
