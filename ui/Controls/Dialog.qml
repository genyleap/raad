/*!
    \file        Dialog.qml
    \brief       Implements the Dialog QML component for RAAD.
    \details     This file contains the Dialog user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Effects

import Raad

Dialog {
    id: control

    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(appRoot.width, 420)
    modal: true
    focus: true
    padding: 0

    property color styleColor: Colors.primary
    property string type: "default"
    property string iconText: "\uf06a"

    property string desc: ""
    property string message: ""

    property string okTextOverride: ""
    property string openTextOverride: ""
    property string saveTextOverride: ""
    property string saveAllTextOverride: ""
    property string cancelTextOverride: ""
    property string closeTextOverride: ""
    property string discardTextOverride: ""
    property string applyTextOverride: ""
    property string resetTextOverride: ""
    property string restoreDefaultsTextOverride: ""
    property string helpTextOverride: ""
    property string yesTextOverride: ""
    property string yesToAllTextOverride: ""
    property string noTextOverride: ""
    property string noToAllTextOverride: ""
    property string abortTextOverride: ""
    property string retryTextOverride: ""
    property string ignoreTextOverride: ""

    property string okStyleOverride: ""
    property string openStyleOverride: ""
    property string saveStyleOverride: ""
    property string saveAllStyleOverride: ""
    property string cancelStyleOverride: ""
    property string closeStyleOverride: ""
    property string discardStyleOverride: ""
    property string applyStyleOverride: ""
    property string resetStyleOverride: ""
    property string restoreDefaultsStyleOverride: ""
    property string helpStyleOverride: ""
    property string yesStyleOverride: ""
    property string yesToAllStyleOverride: ""
    property string noStyleOverride: ""
    property string noToAllStyleOverride: ""
    property string abortStyleOverride: ""
    property string retryStyleOverride: ""
    property string ignoreStyleOverride: ""

    default property alias bodyData: bodyContainer.data

    readonly property bool hasFooter: footerButtonsModel.count > 0
    readonly property real headerSpacing: Metrics.padding
    readonly property real contentSpacing: Metrics.padding * 1.25
    readonly property real dialogPadding: Metrics.padding * 2
    readonly property real footerImplicitHeight: hasFooter ? 64 : 0
    readonly property real contentViewportHeight: Math.max(120,
                                                           control.height
                                                           - columnHeader.implicitHeight
                                                           - footerImplicitHeight
                                                           - Metrics.padding * 2.5)

    title: "About"

    implicitHeight: columnHeader.implicitHeight
                    + contentColumn.implicitHeight
                    + footerImplicitHeight
                    + dialogPadding * 2

    function updateStyle() {
        switch (type) {
        case "info":
            styleColor = Colors.primary
            iconText = "\uf06a"
            break
        case "warning":
            styleColor = Colors.warning
            iconText = "\uf071"
            break
        case "success":
            styleColor = Colors.success
            iconText = "\uf2f7"
            break
        case "danger":
            styleColor = Colors.error
            iconText = "\ue12e"
            break
        case "new":
            styleColor = Colors.primary
            iconText = "\ue494"
            break
        default:
            styleColor = Colors.primary
            iconText = "\uf05a"
            break
        }
    }

    function clearFooterButtons() {
        footerButtonsModel.clear()
    }

    function appendFooterButton(flag, fallbackText, role) {
        footerButtonsModel.append({
                                      buttonFlag: flag,
                                      buttonText: buttonTextFor(flag, fallbackText),
                                      buttonRole: role,
                                      buttonStyle: buttonStyleFor(flag, role)
                                  })
    }

    function isPositiveRole(role) {
        return role === "accept" || role === "apply" || role === "yes"
    }

    function isNegativeRole(role) {
        return role === "reject" || role === "no"
    }

    function buttonTextFor(flag, fallbackText) {
        switch (flag) {
        case Dialog.Ok:
            return okTextOverride.length > 0 ? okTextOverride : fallbackText
        case Dialog.Open:
            return openTextOverride.length > 0 ? openTextOverride : fallbackText
        case Dialog.Save:
            return saveTextOverride.length > 0 ? saveTextOverride : fallbackText
        case Dialog.SaveAll:
            return saveAllTextOverride.length > 0 ? saveAllTextOverride : fallbackText
        case Dialog.Cancel:
            return cancelTextOverride.length > 0 ? cancelTextOverride : fallbackText
        case Dialog.Close:
            return closeTextOverride.length > 0 ? closeTextOverride : fallbackText
        case Dialog.Discard:
            return discardTextOverride.length > 0 ? discardTextOverride : fallbackText
        case Dialog.Apply:
            return applyTextOverride.length > 0 ? applyTextOverride : fallbackText
        case Dialog.Reset:
            return resetTextOverride.length > 0 ? resetTextOverride : fallbackText
        case Dialog.RestoreDefaults:
            return restoreDefaultsTextOverride.length > 0 ? restoreDefaultsTextOverride : fallbackText
        case Dialog.Help:
            return helpTextOverride.length > 0 ? helpTextOverride : fallbackText
        case Dialog.Yes:
            return yesTextOverride.length > 0 ? yesTextOverride : fallbackText
        case Dialog.YesToAll:
            return yesToAllTextOverride.length > 0 ? yesToAllTextOverride : fallbackText
        case Dialog.No:
            return noTextOverride.length > 0 ? noTextOverride : fallbackText
        case Dialog.NoToAll:
            return noToAllTextOverride.length > 0 ? noToAllTextOverride : fallbackText
        case Dialog.Abort:
            return abortTextOverride.length > 0 ? abortTextOverride : fallbackText
        case Dialog.Retry:
            return retryTextOverride.length > 0 ? retryTextOverride : fallbackText
        case Dialog.Ignore:
            return ignoreTextOverride.length > 0 ? ignoreTextOverride : fallbackText
        default:
            return fallbackText
        }
    }

    function buttonStyleOverrideFor(flag) {
        switch (flag) {
        case Dialog.Ok:
            return okStyleOverride
        case Dialog.Open:
            return openStyleOverride
        case Dialog.Save:
            return saveStyleOverride
        case Dialog.SaveAll:
            return saveAllStyleOverride
        case Dialog.Cancel:
            return cancelStyleOverride
        case Dialog.Close:
            return closeStyleOverride
        case Dialog.Discard:
            return discardStyleOverride
        case Dialog.Apply:
            return applyStyleOverride
        case Dialog.Reset:
            return resetStyleOverride
        case Dialog.RestoreDefaults:
            return restoreDefaultsStyleOverride
        case Dialog.Help:
            return helpStyleOverride
        case Dialog.Yes:
            return yesStyleOverride
        case Dialog.YesToAll:
            return yesToAllStyleOverride
        case Dialog.No:
            return noStyleOverride
        case Dialog.NoToAll:
            return noToAllStyleOverride
        case Dialog.Abort:
            return abortStyleOverride
        case Dialog.Retry:
            return retryStyleOverride
        case Dialog.Ignore:
            return ignoreStyleOverride
        default:
            return ""
        }
    }

    function contextualPositiveStyle() {
        switch (type) {
        case "danger":
            return "danger"
        case "warning":
            return "warning"
        case "success":
            return "success"
        case "info":
            return "info"
        default:
            return "default"
        }
    }

    function buttonStyleFor(flag, role) {
        const explicitStyle = buttonStyleOverrideFor(flag)
        if (explicitStyle.length > 0)
            return explicitStyle

        switch (flag) {
        case Dialog.Discard:
        case Dialog.Abort:
            return "danger"

        case Dialog.Ignore:
            return "warning"

        case Dialog.Help:
        case Dialog.Retry:
            return "info"

        case Dialog.Reset:
        case Dialog.RestoreDefaults:
            return "default"

        case Dialog.Cancel:
        case Dialog.Close:
        case Dialog.No:
        case Dialog.NoToAll:
            return "default"

        case Dialog.Ok:
        case Dialog.Open:
        case Dialog.Save:
        case Dialog.SaveAll:
        case Dialog.Apply:
        case Dialog.Yes:
        case Dialog.YesToAll:
            return contextualPositiveStyle()

        default:
            if (role === "help")
                return "info"
            if (role === "discard")
                return "danger"
            if (role === "reset")
                return "default"
            if (isPositiveRole(role))
                return contextualPositiveStyle()
            if (isNegativeRole(role))
                return "default"
            return "default"
        }
    }

    function handleStandardButton(role, flag) {
        switch (role) {
        case "accept":
            control.accept()
            break
        case "reject":
            control.reject()
            break
        case "apply":
            control.applied()
            break
        case "reset":
            control.reset()
            break
        case "help":
            control.helpRequested()
            break
        case "discard":
            control.discarded()
            control.close()
            break
        case "yes":
            control.accept()
            break
        case "no":
            control.reject()
            break
        default:
            control.close()
            break
        }
    }

    function rebuildFooterButtons() {
        clearFooterButtons()

        if (standardButtons & Dialog.Help)
            appendFooterButton(Dialog.Help, qsTr("Help"), "help")

        if (standardButtons & Dialog.Reset)
            appendFooterButton(Dialog.Reset, qsTr("Reset"), "reset")

        if (standardButtons & Dialog.RestoreDefaults)
            appendFooterButton(Dialog.RestoreDefaults, qsTr("Reset"), "reset")

        if (standardButtons & Dialog.Abort)
            appendFooterButton(Dialog.Abort, qsTr("Abort"), "reject")

        if (standardButtons & Dialog.Close)
            appendFooterButton(Dialog.Close, qsTr("Close"), "reject")

        if (standardButtons & Dialog.Cancel)
            appendFooterButton(Dialog.Cancel, qsTr("Cancel"), "reject")

        if (standardButtons & Dialog.No)
            appendFooterButton(Dialog.No, qsTr("No"), "no")

        if (standardButtons & Dialog.NoToAll)
            appendFooterButton(Dialog.NoToAll, qsTr("No to All"), "no")

        if (standardButtons & Dialog.Ignore)
            appendFooterButton(Dialog.Ignore, qsTr("Ignore"), "accept")

        if (standardButtons & Dialog.Retry)
            appendFooterButton(Dialog.Retry, qsTr("Retry"), "accept")

        if (standardButtons & Dialog.Discard)
            appendFooterButton(Dialog.Discard, qsTr("Discard"), "discard")

        if (standardButtons & Dialog.Apply)
            appendFooterButton(Dialog.Apply, qsTr("Apply"), "apply")

        if (standardButtons & Dialog.Ok)
            appendFooterButton(Dialog.Ok, qsTr("OK"), "accept")

        if (standardButtons & Dialog.Open)
            appendFooterButton(Dialog.Open, qsTr("Open"), "accept")

        if (standardButtons & Dialog.Save)
            appendFooterButton(Dialog.Save, qsTr("Save"), "accept")

        if (standardButtons & Dialog.SaveAll)
            appendFooterButton(Dialog.SaveAll, qsTr("Save All"), "accept")

        if (standardButtons & Dialog.Yes)
            appendFooterButton(Dialog.Yes, qsTr("Yes"), "yes")

        if (standardButtons & Dialog.YesToAll)
            appendFooterButton(Dialog.YesToAll, qsTr("Yes to All"), "yes")
    }

    onOkTextOverrideChanged: rebuildFooterButtons()
    onOpenTextOverrideChanged: rebuildFooterButtons()
    onSaveTextOverrideChanged: rebuildFooterButtons()
    onSaveAllTextOverrideChanged: rebuildFooterButtons()
    onCancelTextOverrideChanged: rebuildFooterButtons()
    onCloseTextOverrideChanged: rebuildFooterButtons()
    onDiscardTextOverrideChanged: rebuildFooterButtons()
    onApplyTextOverrideChanged: rebuildFooterButtons()
    onResetTextOverrideChanged: rebuildFooterButtons()
    onRestoreDefaultsTextOverrideChanged: rebuildFooterButtons()
    onHelpTextOverrideChanged: rebuildFooterButtons()
    onYesTextOverrideChanged: rebuildFooterButtons()
    onYesToAllTextOverrideChanged: rebuildFooterButtons()
    onNoTextOverrideChanged: rebuildFooterButtons()
    onNoToAllTextOverrideChanged: rebuildFooterButtons()
    onAbortTextOverrideChanged: rebuildFooterButtons()
    onRetryTextOverrideChanged: rebuildFooterButtons()
    onIgnoreTextOverrideChanged: rebuildFooterButtons()

    onOkStyleOverrideChanged: rebuildFooterButtons()
    onOpenStyleOverrideChanged: rebuildFooterButtons()
    onSaveStyleOverrideChanged: rebuildFooterButtons()
    onSaveAllStyleOverrideChanged: rebuildFooterButtons()
    onCancelStyleOverrideChanged: rebuildFooterButtons()
    onCloseStyleOverrideChanged: rebuildFooterButtons()
    onDiscardStyleOverrideChanged: rebuildFooterButtons()
    onApplyStyleOverrideChanged: rebuildFooterButtons()
    onResetStyleOverrideChanged: rebuildFooterButtons()
    onRestoreDefaultsStyleOverrideChanged: rebuildFooterButtons()
    onHelpStyleOverrideChanged: rebuildFooterButtons()
    onYesStyleOverrideChanged: rebuildFooterButtons()
    onYesToAllStyleOverrideChanged: rebuildFooterButtons()
    onNoStyleOverrideChanged: rebuildFooterButtons()
    onNoToAllStyleOverrideChanged: rebuildFooterButtons()
    onAbortStyleOverrideChanged: rebuildFooterButtons()
    onRetryStyleOverrideChanged: rebuildFooterButtons()
    onIgnoreStyleOverrideChanged: rebuildFooterButtons()

    Component.onCompleted: {
        updateStyle()
        rebuildFooterButtons()
    }

    onTypeChanged: {
        updateStyle()
        rebuildFooterButtons()
    }

    onStandardButtonsChanged: rebuildFooterButtons()

    anchors.centerIn: Overlay.overlay
    transformOrigin: Item.Center
    scale: visible ? 1.0 : 0.94

    Overlay.modal: Rectangle {
        id: dimLayer
        color: Colors.staticSecondry
        opacity: 0.8
        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

    }

    background: Rectangle {
        id: backgroundDialog
        radius: Metrics.cornerRadius

        Shadow {
            z: -1
        }

        gradient: LinearGradient {
            GradientStop {
                position: 0.8
                color: Colors.lightMode
                       ? Qt.darker(Colors.gradientPrimary, 0.8)
                       : Qt.lighter(Colors.gradientPrimary, 1.5)
            }
            GradientStop {
                position: 0.0
                color: Colors.lightMode
                       ? Qt.lighter(Colors.gradientPrimary, 0.8)
                       : Qt.lighter(Colors.gradientSecondry, 0.8)
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Metrics.cornerRadius
            color: Colors.backgroundActivated
        }
    }

    header: ColumnLayout {
        id: columnHeader
        width: parent.width
        spacing: 0

        Item {
            Layout.preferredHeight: control.dialogPadding * 1.50
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: control.dialogPadding
            Layout.rightMargin: control.dialogPadding
            spacing: control.headerSpacing

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                text: control.title
                font.family: FontSystem.getContentFont.name
                font.weight: Font.Bold
                font.pixelSize: Typography.h3
                color: Colors.textPrimary
                elide: Text.ElideRight
            }

            Text {
                visible: control.iconText.length > 0
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                text: control.iconText
                font.family: FontSystem.getAwesomeLight.name
                font.pixelSize: Typography.h2
                font.weight: Font.Light
                color: control.styleColor
            }
        }

        Item {
            Layout.preferredHeight: Metrics.padding * 2.75
        }

        HorizontalLine {}
    }

    footer: Rectangle {
        implicitHeight: control.hasFooter ? control.footerImplicitHeight : 0
        visible: control.hasFooter
        color: Colors.backgroundItemActivated
        bottomLeftRadius: Metrics.cornerRadius
        bottomRightRadius: Metrics.cornerRadius

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: control.dialogPadding
            anchors.rightMargin: control.dialogPadding
            anchors.topMargin: Metrics.padding
            anchors.bottomMargin: Metrics.padding * 1.25
            spacing: Metrics.padding

            Item {
                Layout.fillWidth: true
            }

            Repeater {
                model: footerButtonsModel

                delegate: Button {
                    required property string buttonText
                    required property string buttonRole
                    required property string buttonStyle
                    required property int buttonFlag

                    text: buttonText
                    style: buttonStyle
                    isDefault: buttonStyle !== "default"
                               || buttonRole === "accept"
                               || buttonRole === "apply"
                               || buttonRole === "yes"
                               || buttonRole === "discard"

                    onClicked: {
                        control.handleStandardButton(buttonRole, buttonFlag)
                    }
                }
            }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "scale"
            from: 0.94
            to: 1.0
            duration: Animations.fast * 1.15
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "scale"
            from: 1.0
            to: 0.94
            duration: Animations.fast * 0.9
            easing.type: Easing.InCubic
        }
    }

    contentItem: Item {
        implicitHeight: Math.min(contentColumn.implicitHeight, control.contentViewportHeight)
        implicitWidth: contentColumn.implicitWidth
        height: Math.min(contentColumn.implicitHeight, control.contentViewportHeight)
        clip: true

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.leftMargin: control.dialogPadding
            anchors.rightMargin: control.dialogPadding
            anchors.topMargin: Metrics.padding * 1.5
            spacing: control.contentSpacing

            Text {
                visible: control.desc.length > 0
                text: control.desc
                font.bold: true
                Layout.fillWidth: true
            }

            Text {
                visible: control.message.length > 0
                text: control.message
                font.family: FontSystem.getContentFontRegular.name
                font.weight: Font.Normal
                font.pixelSize: Typography.h5
                color: Colors.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                id: bodyContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: control.contentSpacing
            }

            Item {
                visible: !control.hasFooter
                Layout.preferredHeight: Metrics.padding
            }
        }
    }

    ListModel {
        id: footerButtonsModel
    }
}
