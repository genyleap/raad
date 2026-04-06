/*!
    \file        Button.qml
    \brief       Implements the Button QML component for RAAD.
    \details     This file contains the Button user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts
import QtQuick.Effects

import Raad

T.Button {
    id: control

    property bool isDefault : true
    property bool isBold : false

    property string setIconBegin : ""
    property string setIconEnd : ""
    property string style : "default"
    property color styleColor : Colors.primary
    property string sizeType : "normal"

    function resolvedStyleColor() {
        if (style === "default")
            return Colors.accentPrimary
        if (style === "info")
            return Colors.secondry
        if (style === "warning")
            return Colors.warning
        if (style === "success")
            return Colors.success
        if (style === "danger")
            return Colors.error
        return styleColor
    }

    function hasDefault() {
        if(style == "default" && isDefault)
            return true
        else
            return false
    }

    width: (setIconEnd || setIconBegin) ? 110 * 1.5 : 86
    implicitHeight: 38
    Layout.fillWidth: false
    implicitWidth: 110

    opacity: control.enabled ? 1 : 0.5

    contentItem: Item {

        anchors.fill: parent

        Text {
            visible: (!control.setIconEnd || !control.setIconBegin)
            text: control.text
            font.family: FontSystem.getTitleBoldFont.font.family
            font.pixelSize: Typography.t2
            font.bold: control.isBold ? Font.Bold : Font.Normal
            font.weight: control.isBold ? Font.Bold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: control.setIconEnd ? Text.AlignLeft : Text.AlignHCenter

            anchors.fill: parent
            anchors.left: parent.left
            anchors.leftMargin: control.setIconEnd ? 13 : 0
            anchors.right: parent.right
            anchors.rightMargin: control.setIconBegin ? -80 : 0
            anchors.topMargin: 1 // patch fix for unsupported font [latin] position.
            color: {
                if(control.hasDefault())
                    Colors.staticPrimary
                if (control.hasDefault() && control.isDefault && !Colors.lightMode)
                    Colors.staticSecondry
                else if (!control.isDefault && Colors.lightMode)
                    Colors.staticSecondry
                else if (!control.isDefault && Colors.lightMode)
                    Colors.staticSecondry
                else
                    Colors.staticPrimary
            }

            Behavior on color {
                ColorAnimation {
                    duration: Animations.normal;
                    easing.type: Easing.Linear;
                }
            }
            elide: Text.ElideRight

            scale: control.pressed ? 0.9 : 1.0
            Behavior on scale { NumberAnimation { duration: 200; } }
        }

        RowLayout {
            visible: (control.setIconEnd || control.setIconBegin)
            anchors.fill: parent

            Item { Layout.preferredWidth: 10; }

            Text {
                Layout.topMargin: -1 // patch fix for unsupported font [latin] position.
                font.family: FontSystem.getAwesomeRegular.name
                text: control.setIconBegin
                font.pixelSize: Typography.t2
                font.bold: control.isBold ? Font.Bold : Font.Normal
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                font.weight: control.isBold ? Font.Bold : Font.Normal
                color: {
                    if(control.hasDefault())
                        Colors.staticPrimary
                    if (control.hasDefault() && !Colors.lightMode)
                        Colors.staticSecondry
                    else
                        Colors.staticPrimary
                }
                visible: control.setIconBegin ? true : false
            }

            Item { Layout.preferredWidth: 5; }

            Item { Layout.fillWidth: true; }

            Text {
                Layout.topMargin: -1 // patch fix for unsupported font [latin] position.
                font.family: FontSystem.getAwesomeRegular.name
                text: control.setIconEnd
                font.pixelSize: Typography.t2
                font.bold: control.isBold ? Font.Bold : Font.Normal
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                font.weight: control.isBold ? Font.Bold : Font.Normal
                color: {
                    if(control.hasDefault())
                        Colors.staticPrimary
                    if (control.hasDefault() && !Colors.lightMode)
                        Colors.staticSecondry
                    else
                        Colors.staticPrimary
                }
                visible: control.setIconEnd ? true : false
            }

            Item { Layout.preferredWidth: 10; }

            scale: control.pressed ? 0.9 : 1.0
            Behavior on scale { NumberAnimation { duration: 200; } }

        }

    }

    background: Rectangle {
        id: backgroundButton

        Shadow {}

        implicitWidth: control.width
        implicitHeight: control.height
        Layout.fillWidth: true
        radius: width
        color: !control.isDefault ? Colors.backgroundActivated : resolvedStyleColor()
        border.width: 1
        border.color: !control.isDefault ? Colors.borderActivated : resolvedStyleColor()
    }

    MouseArea {
        id: mouseArea
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
        anchors.fill: parent
    }
}
