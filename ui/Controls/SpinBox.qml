/*!
    \file        SpinBox.qml
    \brief       Implements the SpinBox QML component for RAAD.
    \details     This file contains the SpinBox user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Effects

import Raad

T.SpinBox {
    id: control

    property bool sideButtons: true

    readonly property int buttonWidth: 32
    implicitHeight: 40
    implicitWidth: {
        // base field width + room for buttons
        const base = Math.max(62, implicitBackgroundWidth + leftInset + rightInset)
        return sideButtons ? Math.max(base, 62 + buttonWidth * 2) : base
    }

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    editable: true

    // Text padding (optical centering)
    leftPadding: sideButtons ? (buttonWidth + Metrics.padding * 2) : (Metrics.padding * 2)
    rightPadding: sideButtons ? (buttonWidth + Metrics.padding * 2) : (buttonWidth + Metrics.padding + 8)
    topPadding: 10
    bottomPadding: 10

    leftInset: 0
    rightInset: 0

    validator: IntValidator {
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    textFromValue: function(value, locale) {
        return Number(value).toLocaleString(locale, "f", 0)
    }

    valueFromText: function(text, locale) {
        return Number.fromLocaleString(locale, text)
    }

    contentItem: TextInput {
        id: valueInput
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font.pixelSize: 15
        font.weight: Font.DemiBold
        color: control.enabled ? Colors.textPrimary : Colors.textMuted
        selectionColor: Colors.secondryBack
        selectedTextColor: Colors.textPrimary
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        clip: true

        Connections {
            target: control
            function onValueChanged() {
                valueInput.text = control.textFromValue(control.value, control.locale)
            }
        }
    }

    // --- Field background
    background: Rectangle {
        id: backgrdounSpin
        radius: width
        antialiasing: true

        color: {
            if (!control.enabled) return Colors.backgroundItemDeactivated
            if (control.activeFocus || control.up.pressed || control.down.pressed) return Colors.backgroundItemFocused
            if (control.hovered || control.up.hovered || control.down.hovered) return Colors.backgroundItemHovered
            return Colors.backgroundItemActivated
        }

        border.width: 1
        border.color: {
            if (!control.enabled) return Colors.borderDeactivated
            if (control.visualFocus || control.activeFocus || control.up.pressed || control.down.pressed) return Colors.borderFocused
            if (control.hovered || control.up.hovered || control.down.hovered) return Colors.borderHovered
            return Colors.borderActivated
        }

        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }

        Shadow {}
    }

    // ============================
    // Side buttons (row): [-] [ + ]
    // ============================

    // Left button background (DOWN / decrement)
    Rectangle {
        id: leftBtnBg
        visible: control.sideButtons
        width: control.buttonWidth
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 6

        // only left corners rounded
        topLeftRadius: width
        bottomLeftRadius: width
        topRightRadius: 0
        bottomRightRadius: 0

        antialiasing: true
        clip: true
        // color: Colors.backgroundItemActivated
        color: "transparent"

        Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }

    // Right button background (UP / increment)
    Rectangle {
        id: rightBtnBg
        visible: control.sideButtons
        width: control.buttonWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 6

        topRightRadius: width
        bottomRightRadius: width
        topLeftRadius: 0
        bottomLeftRadius: 0

        antialiasing: true
        clip: true
        // color: Colors.pagespaceActivated
        color: "transparent"


        Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }

    // Divider lines (optional, subtle)
    Rectangle {
        visible: control.sideButtons
        width: 1
        anchors.left: leftBtnBg.right
        anchors.top: leftBtnBg.top
        anchors.bottom: leftBtnBg.bottom
        color: Colors.borderActivated
        opacity: 0.35
    }
    Rectangle {
        visible: control.sideButtons
        width: 1
        anchors.right: rightBtnBg.left
        anchors.top: rightBtnBg.top
        anchors.bottom: rightBtnBg.bottom
        color: Colors.borderActivated
        opacity: 0.35
    }

    // --- DOWN indicator (left button)
    down.indicator: Item {
        id: downIndicator
        parent: control.sideButtons ? leftBtnBg : null
        visible: control.sideButtons
        anchors.fill: parent

        readonly property bool hovered: !!control.down.hovered
        readonly property bool pressed: !!control.down.pressed

        Rectangle {
            anchors.fill: parent
            antialiasing: true

            topLeftRadius: appRootObjects.isLeftToRight ? width : 0
            bottomLeftRadius: appRootObjects.isLeftToRight ? width : 0
            topRightRadius: appRootObjects.isLeftToRight ? 0 : width
            bottomRightRadius: appRootObjects.isLeftToRight ? 0 : width

            color: downIndicator.pressed ? Colors.pagespacePressed
                                         : downIndicator.hovered ? Colors.pagespaceHovered
                                                                 : "transparent"

            Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        }

        // minus icon
        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 2
            radius: 1
            color: control.enabled ? Colors.textSecondary : Colors.textMuted

            scale: downIndicator.pressed ? 0.92 : 1.0
            Behavior on scale { NumberAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        }
    }

    // --- UP indicator (right button)
    up.indicator: Item {
        id: upIndicator
        parent: control.sideButtons ? rightBtnBg : null
        visible: control.sideButtons
        anchors.fill: parent

        readonly property bool hovered: !!control.up.hovered
        readonly property bool pressed: !!control.up.pressed

        Rectangle {
            anchors.fill: parent
            antialiasing: true

            topRightRadius: appRootObjects.isLeftToRight ? width : 0
            bottomRightRadius: appRootObjects.isLeftToRight ? width : 0
            topLeftRadius: appRootObjects.isLeftToRight ? 0 : width
            bottomLeftRadius: appRootObjects.isLeftToRight ? 0 : width

            color: upIndicator.pressed ? Colors.pagespacePressed
                                       : upIndicator.hovered ? Colors.pagespaceHovered
                                                             : "transparent"

            Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        }

        // plus icon
        Item {
            anchors.centerIn: parent
            width: 12
            height: 12

            Rectangle {
                anchors.centerIn: parent
                width: 12
                height: 2
                radius: 1
                color: control.enabled ? Colors.textSecondary : Colors.textMuted
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: 12
                radius: 1
                color: control.enabled ? Colors.textSecondary : Colors.textMuted
            }

            scale: upIndicator.pressed ? 0.92 : 1.0
            Behavior on scale { NumberAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
        }
    }

    // Focus ring
    Rectangle {
        anchors.fill: parent
        radius: width
        antialiasing: true
        color: "transparent"
        border.width: 1
        border.color: Colors.borderFocused
        opacity: control.activeFocus ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }
    }
}
