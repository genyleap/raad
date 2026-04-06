/*!
    \file        ProgressBar.qml
    \brief       Implements the ProgressBar QML component for RAAD.
    \details     This file contains the ProgressBar user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Particles
import QtQuick.Templates as T

import Raad // Colors, Metrics

T.ProgressBar {
    id: control

    implicitWidth: 220
    implicitHeight: 10
    hoverEnabled: true

    property bool animateValueChanges: true
    property bool _valueAnimationReady: false

    property string statusLevel: "Active"
    property bool isError: false
    property bool isPaused: false

    readonly property real progressWidth: Math.max(0, Math.min(contentItem.width, contentItem.width * control.position))
    readonly property real minimumVisibleFillWidth: control.position > 0 ? fill.height : 0
    readonly property real visualFillWidth: control.position > 0
                                          ? Math.max(minimumVisibleFillWidth, progressWidth)
                                          : 0

    Behavior on value {
        enabled: control.animateValueChanges && control._valueAnimationReady
        NumberAnimation {
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    Component.onCompleted: control._valueAnimationReady = true

    function setColor() {
        if (control.statusLevel === "Done")
            return Colors.success;
        if (control.statusLevel === "Paused")
            return Colors.warning;
        if (control.statusLevel === "Error")
            return Colors.error;
        return Colors.secondry;
    }

    background: Rectangle {
        implicitWidth: 220
        implicitHeight: 10
        radius: height / 2
        antialiasing: true
        color: Colors.backgroundItemDeactivated
        border.width: 1
        border.color: Colors.lineBorderActivated
    }

    contentItem: Item {
        id: front
        implicitWidth: 220
        implicitHeight: 10
        clip: true

        Rectangle {
            id: fill
            x: 0
            y: 0
            height: parent.height
            width: control.visualFillWidth
            radius: Math.min(height / 2, width / 2)
            antialiasing: true
            visible: width > 0

            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.lighter(control.setColor(), 0.9) }
                GradientStop { position: 1.0; color: control.setColor() }
            }

            Behavior on width {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }

            ParticleSystem {
                id: particles
                anchors.fill: parent
                running: control.statusLevel !== "Done" && fill.width > fill.height

                ImageParticle {
                    system: particles
                    anchors.fill: parent
                    source: "qrc:///particleresources/glowdot.png"
                    colorVariation: 1
                    color: "#00ffffff"
                }

                Emitter {
                    system: particles
                    width: Math.max(0, fill.width - 8)
                    height: parent.height
                    x: 4
                    y: 0
                    emitRate: 32
                    lifeSpan: 1024

                    velocity: PointDirection {
                        x: 0
                        xVariation: 256
                    }

                    size: 1
                    sizeVariation: 3
                    endSize: 6
                }
            }
        }

        Rectangle {
            anchors.left: fill.left
            anchors.top: fill.top
            anchors.bottom: fill.bottom
            width: fill.width
            radius: Math.min(height / 2, width / 2)
            antialiasing: true
            opacity: 0.22
            visible: fill.width > 2

            gradient: Gradient {
                GradientStop { position: 0.0; color: "#22FFFFFF" }
                GradientStop { position: 1.0; color: "transparent" }
            }

            Behavior on width {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
