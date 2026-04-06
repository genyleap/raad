/*!
    \file        ComboBox.qml
    \brief       Implements the ComboBox QML component for RAAD.
    \details     This file contains the ComboBox user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import QtQuick.Templates as T

import Raad

T.ComboBox {
    id: control

    implicitWidth: Math.max(180, implicitBackgroundWidth + leftInset + rightInset)
    implicitHeight: Math.max(40, implicitBackgroundHeight + topInset + bottomInset)

    padding: Metrics.padding
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    // Search feature (off by default)
    property bool searchable: false
    property string searchText: ""

    // Where the popup should live (Overlay if available, otherwise Window content)
    readonly property Item popupParent: (Overlay.overlay ? Overlay.overlay : (control.window ? control.window.contentItem : null))

    // Normalized popup source-index list (maps popup row -> ComboBox row)
    property var filteredIndexes: []

    function toText(v) {
        if (v === null || v === undefined) return ""
        if (typeof v === "string") return v
        if (typeof v === "object") {
            if (v.text !== undefined) return String(v.text)
            if (v.name !== undefined) return String(v.name)
            if (v.title !== undefined) return String(v.title)
        }
        return String(v)
    }

    function modelCount() {
        var c = Number(control.count)
        if (isFinite(c) && c > 0) return c

        var m = control.model
        if (!m) return 0

        if (m.count !== undefined) {
            var mc = Number(m.count)
            if (isFinite(mc) && mc >= 0) return mc
        }
        if (m.length !== undefined) {
            var ml = Number(m.length)
            if (isFinite(ml) && ml >= 0) return ml
        }
        return 0
    }

    function modelTextAt(i) {
        if (i < 0) return ""

        // Preferred path for standard ComboBox internals.
        if (i < control.count) {
            var t = control.textAt(i)
            if (t !== undefined && t !== null && String(t).length > 0)
                return toText(t)
        }

        // Fallbacks for raw models.
        var m = control.model
        if (!m) return ""

        if (m.get !== undefined && m.count !== undefined && i < Number(m.count))
            return toText(m.get(i))
        if (m.length !== undefined && i < Number(m.length))
            return toText(m[i])

        return ""
    }

    function rebuildFilter() {
        var next = []

        var term = (control.searchable ? control.searchText.trim().toLowerCase() : "")
        var doFilter = term.length > 0

        var c = modelCount()
        for (var i = 0; i < c; ++i) {
            var t = modelTextAt(i)
            if (!doFilter || t.toLowerCase().indexOf(term) !== -1)
                next.push(i)
        }

        filteredIndexes = next

        if (control.currentIndex < 0 && c > 0 && !doFilter) {
            control.currentIndex = 0
        }
    }

    Component.onCompleted: rebuildFilter()
    onModelChanged: rebuildFilter()
    onCountChanged: {
        if (control.currentIndex < 0 && control.count > 0) {
            control.currentIndex = 0
        }
        rebuildFilter()
    }
    onSearchTextChanged: rebuildFilter()
    onSearchableChanged: {
        if (!searchable) searchText = ""
        rebuildFilter()
    }

    // --- main field background
    background: Rectangle {
        id: backgroundCombobox
        implicitWidth: 180
        implicitHeight: 40
        radius: width

        color: {
            if (!control.enabled) return Colors.backgroundItemDeactivated
            if (control.down || popup.visible) return Colors.backgroundItemFocused
            if (control.hovered)  return Colors.backgroundItemHovered
            return Colors.backgroundItemActivated
        }

        border.width: 1
        border.color: {
            if (!control.enabled) return Colors.borderDeactivated
            if (control.visualFocus || popup.visible) return Colors.borderFocused
            if (control.hovered) return Colors.borderHovered
            return Colors.borderActivated
        }

        Behavior on opacity { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }
        Behavior on border.width { NumberAnimation { duration: Animations.slow; easing.type: Easing.OutCubic } }

        // RectangularShadow {
        //     anchors.fill: backgroundCombobox
        //     offset.x: 5
        //     offset.y: 5
        //     radius: backgroundCombobox.radius
        //     blur: 32
        //     spread: 1
        //     color: Colors.lightShadow
        //     z: -1
        // }

        Shadow {}
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: 22
        text: (control.currentIndex >= 0)
              ? control.modelTextAt(control.currentIndex)
              : ""
        color: control.enabled ? Colors.textPrimary : Colors.textMuted
        font.pixelSize: Typography.t2
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        implicitWidth: 34
        implicitHeight: control.implicitHeight
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Canvas {
            anchors.centerIn: parent
            width: 10
            height: 6
            property color c: (control.enabled ? Colors.textSecondary : Colors.textMuted)

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                ctx.lineTo(width / 2, height)
                ctx.closePath()
                ctx.fillStyle = c
                ctx.fill()
            }

            rotation: popup.visible ? 180 : 0
            transformOrigin: Item.Center
            Behavior on rotation { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

            onCChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
    }

    // --- popup
    popup: Popup {
        id: popup
        parent: control.popupParent
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        width: control.width
        padding: Metrics.padding

        // Always position under control inside the chosen parent
        onAboutToShow: {
            control.rebuildFilter()

            if (!control.popupParent)
                return

            var p = control.mapToItem(control.popupParent, 0, control.height + 8)
            x = Math.round(p.x)
            y = Math.round(p.y)

            if (control.searchable)
                searchField.forceActiveFocus()
        }

        background: Rectangle {
            id: backgroundPopupBox
            radius: Metrics.innerRadius
            color: Colors.backgroundItemActivated
            border.width: 1
            border.color: Colors.borderActivated

            Behavior on color { ColorAnimation { duration: Animations.normal; easing.type: Easing.OutCubic } }

            Shadow {}
        }

        opacity: 0.0
        onOpened: openAnim.restart()
        onClosed: closeAnim.restart()

        NumberAnimation { id: openAnim;  target: popup; property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
        NumberAnimation { id: closeAnim; target: popup; property: "opacity"; from: 1; to: 0; duration: 120; easing.type: Easing.OutCubic }

        contentItem: Column {
            width: popup.width - popup.padding * 2
            spacing: 8

            TextField {
                id: searchField
                visible: control.searchable
                enabled: control.searchable
                height: 36
                width: parent.width
                placeholderText: "Search..."
                text: control.searchText
                onTextChanged: control.searchText = text

                color: Colors.textPrimary
                placeholderTextColor: Colors.textMuted

                background: Rectangle {
                    radius: Metrics.innerRadius * 0.75
                    color: Colors.backgroundActivated
                    border.width: 1
                    border.color: searchField.activeFocus ? Colors.borderFocused : Colors.borderActivated
                    Behavior on border.color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }
            }

            ListView {
                id: list
                width: parent.width
                clip: true

                model: control.searchable ? control.filteredIndexes.length : control.modelCount()
                readonly property int rowHeight: 40
                readonly property int rowCount: Number(model)
                height: Math.min(320, Math.max(40, rowCount * rowHeight))
                implicitHeight: height

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: T.ItemDelegate {
                    id: del
                    width: list.width
                    height: list.rowHeight
                    padding: Metrics.padding
                    hoverEnabled: true

                    readonly property int sourceIndex: control.searchable
                                                       ? ((index >= 0 && index < control.filteredIndexes.length)
                                                          ? control.filteredIndexes[index]
                                                          : -1)
                                                       : index
                    readonly property string labelText: sourceIndex >= 0 ? control.modelTextAt(sourceIndex) : ""

                    contentItem: Text {
                        topPadding: 4
                        text: del.labelText
                        color: control.enabled ? Colors.textPrimary : Colors.textMuted
                        font.pixelSize: Typography.t2
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Item {
                        anchors.fill: parent

                        // Base row surface (NOT transparent -> avoids hover flicker)
                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.innerRadius * 0.7
                            color: Colors.backgroundItemActivated
                            opacity: 0.0001  // effectively invisible but still “there”
                        }

                        // Hover layer (animate opacity, not color)
                        Rectangle {
                            id: hoverLayer
                            anchors.fill: parent
                            radius: Metrics.innerRadius * 0.7
                            color: Colors.backgroundItemHovered

                            opacity: (del.hovered || del.highlighted) ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        }

                        // Press layer
                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.innerRadius * 0.7
                            color: Colors.backgroundItemActivated

                            opacity: del.down ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        }

                        // Subtle border only on hover/highlight (also opacity-animated)
                        Rectangle {
                            anchors.fill: parent
                            radius: Metrics.innerRadius * 0.7
                            color: "transparent"
                            border.width: 1
                            border.color: Colors.borderHovered

                            opacity: (del.hovered || del.highlighted) ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        }

                    }

                    onClicked: {
                        if (sourceIndex >= 0) {
                            control.currentIndex = sourceIndex
                            control.activated(sourceIndex)
                            popup.close()
                        }
                    }
                }
            }
        }
    }

    // Focus ring
    Rectangle {
        anchors.fill: parent
        radius: Metrics.innerRadius
        color: "transparent"
        border.width: 2
        border.color: Colors.borderFocused
        opacity: control.visualFocus ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
}
