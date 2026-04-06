/*!
    \file        DownloadDelegate.qml
    \brief       Implements the DownloadDelegate QML component for RAAD.
    \details     This file contains the DownloadDelegate user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import "../Core" as Core

Item {
    id: root

    property int row: -1
    property string fileName: ""
    property string status: ""
    property real bytesReceived: 0
    property real bytesTotal: 0
    property string queueName: ""
    property string category: ""
    property var task: null

    property bool selected: false
    property bool filterAccepted: true
    property bool hovered: false
    property real nameWidth: Math.max(320, root.width * 0.32)
    property real queueWidth: 110
    property real sizeWidth: 160
    property real statusWidth: 118
    property real etaWidth: 96
    property real speedWidth: 110
    property real segmentsWidth: 92
    property real categoryWidth: 98
    property real actionsWidth: 184

    signal selectRequested(int row,
                           var taskObj,
                           string queue,
                           string category)
    signal pauseResumeRequested(int row)
    signal removeRequested(int row)
    signal openRequested(int row)
    signal contextActionRequested(int row, var taskObj, string action)
    signal detailsRequested(int row,
                           var taskObj,
                           string queue,
                           string category)

    visible: filterAccepted
    height: filterAccepted ? 78 : 0
    width: ListView.view ? ListView.view.width : 100

    Behavior on height {
        NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
    }

    function baseName(path) {
        if (!path || path.length === 0) {
            return "Unknown file"
        }
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        if (slash >= 0 && slash + 1 < path.length) {
            return path.substring(slash + 1)
        }
        return path
    }

    function formatBytes(value) {
        var v = Number(value)
        if (!isFinite(v) || v < 0) {
            v = 0
        }
        const units = ["B", "KB", "MB", "GB", "TB"]
        var i = 0
        while (v >= 1024 && i < units.length - 1) {
            v /= 1024
            i += 1
        }
        const digits = v >= 100 ? 0 : (v >= 10 ? 1 : 2)
        return v.toFixed(digits) + " " + units[i]
    }

    function formatSpeed(value) {
        return formatBytes(value) + "/s"
    }

    function formatEta(seconds) {
        var s = Number(seconds)
        if (!isFinite(s) || s < 0) {
            return "--"
        }
        if (s < 60) {
            return Math.floor(s) + "s"
        }
        const m = Math.floor(s / 60)
        const sec = Math.floor(s % 60)
        if (m < 60) {
            return m + "m " + sec + "s"
        }
        const h = Math.floor(m / 60)
        return h + "h " + (m % 60) + "m"
    }

    function visualState() {
        if (status === "Done" && task && task.checksumState === "Verifying") {
            return "Verifying"
        }
        if (status === "Active" && bytesTotal > 0 && bytesReceived / bytesTotal > 0.998) {
            return "Finalizing"
        }
        if (status === "Paused" && task && task.pauseReason && task.pauseReason.length > 0) {
            return "Paused"
        }
        return status
    }

    function statusTone(text) {
        if (text === "Active") return "success"
        if (text === "Done") return "success"
        if (text.indexOf("Paused") === 0) return "warning"
        if (text === "Error") return "danger"
        if (text === "Canceled") return "muted"
        if (text === "Verifying") return "accent"
        if (text === "Finalizing") return "accent"
        return "secondary"
    }

    readonly property real ratio: bytesTotal > 0 ? Math.min(1.0, bytesReceived / bytesTotal) : 0.0
    readonly property string resolvedState: visualState()
    readonly property string urlText: task ? task.url() : ""

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: root.selected
               ? Core.Colors.alpha(Core.Colors.selected, 0.8)
               : (root.hovered ? Core.Colors.alpha(Core.Colors.panelAlt, 0.9) : "transparent")
        border.width: 1
        border.color: root.selected ? Core.Colors.borderStrong : Core.Colors.alpha(Core.Colors.border, 0.45)

        Behavior on color {
            ColorAnimation { duration: 90 }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            onEntered: root.hovered = true
            onExited: root.hovered = false
            onClicked: function(mouse) {
                root.selectRequested(root.row, root.task, root.queueName, root.category)
                if (mouse.button === Qt.RightButton) {
                    rowMenu.popup(mouse.x, mouse.y)
                }
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    root.detailsRequested(root.row, root.task, root.queueName, root.category)
                }
            }
            cursorShape: Qt.PointingHandCursor
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 8
            anchors.bottomMargin: 7
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: root.nameWidth
                    Layout.maximumWidth: root.nameWidth
                    Layout.minimumWidth: root.nameWidth
                    spacing: 1

                    Label {
                        text: root.baseName(root.fileName)
                        role: "caption"
                        elide: Text.ElideRight
                    }

                    Label {
                        text: root.urlText
                        role: "micro"
                        tone: "muted"
                        elide: Text.ElideMiddle
                    }
                }

                Label {
                    Layout.preferredWidth: root.queueWidth
                    Layout.maximumWidth: root.queueWidth
                    Layout.minimumWidth: root.queueWidth
                    text: root.queueName
                    role: "caption"
                    tone: "secondary"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.preferredWidth: root.sizeWidth
                    Layout.maximumWidth: root.sizeWidth
                    Layout.minimumWidth: root.sizeWidth
                    text: root.formatBytes(root.bytesReceived)
                          + (root.bytesTotal > 0 ? " / " + root.formatBytes(root.bytesTotal) : "")
                    role: "mono"
                    tone: "secondary"
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: root.statusWidth
                    Layout.maximumWidth: root.statusWidth
                    Layout.minimumWidth: root.statusWidth
                    implicitHeight: 24
                    radius: 9
                    color: Core.Colors.alpha(Core.Colors.accent, 0.12)
                    border.width: 1
                    border.color: {
                        if (root.statusTone(root.resolvedState) === "danger") return Core.Colors.danger
                        if (root.statusTone(root.resolvedState) === "success") return Core.Colors.success
                        if (root.statusTone(root.resolvedState) === "warning") return Core.Colors.warning
                        if (root.statusTone(root.resolvedState) === "accent") return Core.Colors.accent
                        return Core.Colors.borderStrong
                    }

                    Label {
                        anchors.centerIn: parent
                        text: root.resolvedState
                        role: "micro"
                        tone: root.statusTone(root.resolvedState)
                    }
                }

                Label {
                    Layout.preferredWidth: root.etaWidth
                    Layout.maximumWidth: root.etaWidth
                    Layout.minimumWidth: root.etaWidth
                    text: task ? root.formatEta(task.eta) : "--"
                    role: "mono"
                    tone: "secondary"
                }

                Label {
                    Layout.preferredWidth: root.speedWidth
                    Layout.maximumWidth: root.speedWidth
                    Layout.minimumWidth: root.speedWidth
                    text: task ? root.formatSpeed(task.speed) : "0 B/s"
                    role: "mono"
                    tone: task && task.speed > 0 ? "accent" : "secondary"
                }

                Label {
                    Layout.preferredWidth: root.segmentsWidth
                    Layout.maximumWidth: root.segmentsWidth
                    Layout.minimumWidth: root.segmentsWidth
                    text: task ? (task.effectiveSegments() + "/" + task.segments()) : "0/0"
                    role: "mono"
                    tone: "secondary"
                }

                Label {
                    Layout.preferredWidth: root.categoryWidth
                    Layout.maximumWidth: root.categoryWidth
                    Layout.minimumWidth: root.categoryWidth
                    text: root.category
                    role: "caption"
                    tone: "secondary"
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.preferredWidth: root.actionsWidth
                    Layout.maximumWidth: root.actionsWidth
                    Layout.minimumWidth: root.actionsWidth
                    spacing: 6

                    Button {
                        Layout.fillWidth: true
                        text: root.status === "Active" ? "Pause" : "Resume"
                        variant: "secondary"
                        compact: true
                        enabled: root.status === "Active" || root.status === "Paused"
                        onClicked: root.pauseResumeRequested(root.row)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Cancel"
                        variant: "danger"
                        compact: true
                        enabled: root.status === "Active" || root.status === "Paused" || root.status === "Queued"
                        onClicked: root.contextActionRequested(root.row, root.task, "cancel")
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Open"
                        variant: "ghost"
                        compact: true
                        enabled: root.status === "Done"
                        onClicked: root.openRequested(root.row)
                    }
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: root.ratio
                indeterminate: root.bytesTotal <= 0 && root.status === "Active"
                fillColor: root.status === "Error" ? Core.Colors.danger : Core.Colors.accent
            }
        }

        QQC2.Menu {
            id: rowMenu

            QQC2.MenuItem {
                text: root.status === "Active" ? "Stop" : "Resume"
                enabled: root.status === "Active" || root.status === "Paused"
                onTriggered: root.contextActionRequested(root.row, root.task, root.status === "Active" ? "pause" : "resume")
            }

            QQC2.MenuItem {
                text: "Retry"
                enabled: root.task && (root.status === "Error" || root.status === "Canceled" || root.status === "Done")
                onTriggered: root.contextActionRequested(root.row, root.task, "retry")
            }

            QQC2.MenuItem {
                text: "Cancel"
                enabled: root.status === "Active" || root.status === "Paused" || root.status === "Queued"
                onTriggered: root.contextActionRequested(root.row, root.task, "cancel")
            }

            QQC2.MenuSeparator { }

            QQC2.MenuItem {
                text: "Open"
                enabled: root.status === "Done"
                onTriggered: root.contextActionRequested(root.row, root.task, "open")
            }

            QQC2.MenuItem {
                text: "Show in Folder"
                onTriggered: root.contextActionRequested(root.row, root.task, "reveal")
            }

            QQC2.MenuSeparator { }

            QQC2.MenuItem {
                text: "Copy URL"
                enabled: root.task
                onTriggered: root.contextActionRequested(root.row, root.task, "copy_url")
            }

            QQC2.MenuItem {
                text: "Copy Path"
                enabled: root.task
                onTriggered: root.contextActionRequested(root.row, root.task, "copy_path")
            }

            QQC2.MenuItem {
                text: "Verify"
                enabled: root.task
                onTriggered: root.contextActionRequested(root.row, root.task, "verify")
            }

            QQC2.MenuItem {
                text: "Properties"
                enabled: root.task
                onTriggered: root.detailsRequested(root.row, root.task, root.queueName, root.category)
            }

            QQC2.MenuSeparator { }

            QQC2.MenuItem {
                text: "Remove"
                enabled: root.task
                onTriggered: root.contextActionRequested(root.row, root.task, "remove")
            }
        }
    }
}
