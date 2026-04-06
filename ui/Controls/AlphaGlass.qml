/*!
    \file        AlphaGlass.qml
    \brief       Implements the AlphaGlass QML component for RAAD.
    \details     This file contains the AlphaGlass user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Effects

import Raad

Item {
    id: control

    width: 128
    height: 128

    property int radius: 0

    property color setColor : null

    property bool movable: false
    property bool magnifierEnabled: false
    property bool blurEnabled: true
    property real zoom: 1.8
    property real blurAmount: 1.0
    property bool hasBorder : false

    property Item scene : null

    property point scenePos: Qt.point(0, 0)

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function updateMapping() {
        scenePos = mapToItem(scene, 0, 0)
    }

    Component.onCompleted: updateMapping()
    onXChanged: updateMapping()
    onYChanged: updateMapping()

    readonly property real sampleWidth: magnifierEnabled ? width / zoom : width
    readonly property real sampleHeight: magnifierEnabled ? height / zoom : height

    readonly property real sampleX: clamp(
                                        scenePos.x + (width - sampleWidth) * 0.5,
                                        0,
                                        Math.max(0, scene.width - sampleWidth)
                                        )

    readonly property real sampleY: clamp(
                                        scenePos.y + (height - sampleHeight) * 0.5,
                                        0,
                                        Math.max(0, scene.height - sampleHeight)
                                        )

    ShaderEffectSource {
        id: capturedBackground
        anchors.fill: parent
        sourceItem: control.scene
        live: true
        recursive: true
        hideSource: false
        smooth: true
        visible: false

        sourceRect: Qt.rect(
                        control.sampleX,
                        control.sampleY,
                        control.sampleWidth,
                        control.sampleHeight
                        )
    }

    RectangularShadow {
        anchors.fill: parent
        offset.x: 0
        offset.y: 10
        radius: control.radius
        blur: 32
        spread: 1
        color: Colors.lightMode ? Colors.lightShadow : Colors.darkShadow //"#22000000"
    }

    Rectangle {
        id: roundedMask
        anchors.fill: parent
        radius: control.radius
        color: Colors.staticPrimary
        layer.enabled: true
        opacity: 0
    }

    MultiEffect {
        anchors.fill: parent
        source: capturedBackground

        blurEnabled: control.blurEnabled
        blur: control.blurAmount
        blurMax: 64

        maskEnabled: true
        maskSource: roundedMask

        autoPaddingEnabled: false

        saturation: control.magnifierEnabled ? 0.3 : 0.0
    }

    Rectangle {
        anchors.fill: parent
        radius: control.radius
        color: control.magnifierEnabled ? "#22ffffff" : Colors.lightMode ? Colors.lightShadow : Colors.darkShadow //"#55ffffff"
        border.width: control.hasBorder ? 1 : 0
        border.color: Colors.borderActivated //"#88ffffff"
    }

    Rectangle {
        anchors.fill: parent
        radius: control.radius
        color: "transparent"
        border.width: control.hasBorder ? 1 : 0
        border.color: "#40ffffff"
        visible: control.magnifierEnabled

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#30ffffff" }
            GradientStop { position: 0.45; color: "#10ffffff" }
            GradientStop { position: 1.0; color: "#05ffffff" }
        }
    }

    MouseArea {
        enabled: control.movable
        anchors.fill: parent
        drag.target: control
        drag.axis: Drag.XAndYAxis
        cursorShape: control.movable ? Qt.OpenHandCursor: null
        onPressed: cursorShape = Qt.ClosedHandCursor
        onReleased: cursorShape = Qt.OpenHandCursor
        onPositionChanged: control.updateMapping()
    }
}
