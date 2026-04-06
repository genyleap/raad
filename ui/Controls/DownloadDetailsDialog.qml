/*!
    \file        DownloadDetailsDialog.qml
    \brief       Implements the DownloadDetailsDialog QML component for RAAD.
    \details     This file contains the DownloadDetailsDialog user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import "../Core" as Core

QQC2.Dialog {
    id: root

    property int row: -1
    property var task: null
    property string queueName: ""
    property string categoryName: ""

    signal pauseResumeRequested(int row)
    signal cancelRequested(int row)
    signal retryRequested(int row)
    signal openRequested(int row)
    signal revealRequested(int row)
    signal removeRequested(int row)
    signal verifyRequested(int row)
    signal setSpeedCapRequested(int row, int bytesPerSec)
    signal copyRequested(string text)

    modal: true
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutsideParent
    standardButtons: QQC2.Dialog.NoButton

    width: Math.min(1060, root.parent ? root.parent.width * 0.9 : 1060)
    height: Math.min(760, root.parent ? root.parent.height * 0.92 : 760)

    readonly property real bytesReceived: row >= 0 ? downloadManager.taskBytesReceived(row) : 0
    readonly property real bytesTotal: row >= 0 ? downloadManager.taskBytesTotal(row) : 0
    readonly property real progressRatio: bytesTotal > 0 ? Math.min(1.0, bytesReceived / bytesTotal) : 0.0
    readonly property real speedValue: task ? task.speed : 0
    readonly property int etaValue: task ? task.eta : -1
    readonly property string statusText: task ? task.stateString : ""

    title: {
        if (!task) return "Download Details"
        return Math.round(progressRatio * 100) + "% " + baseName(task.fileName())
    }

    function baseName(path) {
        if (!path || path.length === 0) return "Unknown"
        const idx = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        if (idx >= 0 && idx + 1 < path.length) return path.substring(idx + 1)
        return path
    }

    function formatBytes(value) {
        var v = Number(value)
        if (!isFinite(v) || v < 0) v = 0
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
        if (!isFinite(s) || s < 0) return "—"
        if (s < 60) return Math.floor(s) + " sec"
        const m = Math.floor(s / 60)
        const sec = Math.floor(s % 60)
        if (m < 60) return m + " min " + sec + " sec"
        const h = Math.floor(m / 60)
        return h + " h " + (m % 60) + " min"
    }

    background: Rectangle {
        radius: 12
        color: Core.Colors.panel
        border.width: 1
        border.color: Core.Colors.borderStrong
    }

    header: Rectangle {
        color: Core.Colors.panelAlt
        border.width: 0
        implicitHeight: 54

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            Label {
                text: root.title
                role: "title"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: statusText
                role: "caption"
                tone: statusText === "Error"
                      ? "danger"
                      : (statusText === "Active" ? "success" : "secondary")
            }
        }
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        QQC2.TabBar {
            id: tabs
            Layout.fillWidth: true

            QQC2.TabButton { text: "Download status" }
            QQC2.TabButton { text: "Speed Limiter" }
            QQC2.TabButton { text: "Options on completion" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Card {
                        Layout.fillWidth: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: task ? task.url() : ""
                                    role: "caption"
                                    tone: "secondary"
                                    elide: Text.ElideMiddle
                                }

                                Button {
                                    text: "Copy URL"
                                    variant: "secondary"
                                    compact: true
                                    enabled: task
                                    onClicked: {
                                        if (task) root.copyRequested(task.url())
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 14
                                rowSpacing: 6

                                Label { text: "Status"; role: "caption"; tone: "secondary" }
                                Label {
                                    text: statusText === "Active" ? "Receiving data..." : statusText
                                    role: "caption"
                                    tone: statusText === "Error"
                                          ? "danger"
                                          : (statusText === "Active" ? "accent" : "secondary")
                                }

                                Label { text: "File size"; role: "caption"; tone: "secondary" }
                                Label { text: formatBytes(bytesTotal); role: "mono" }

                                Label { text: "Downloaded"; role: "caption"; tone: "secondary" }
                                Label {
                                    text: formatBytes(bytesReceived)
                                          + (bytesTotal > 0 ? " (" + (progressRatio * 100).toFixed(2) + " %)" : "")
                                    role: "mono"
                                }

                                Label { text: "Transfer rate"; role: "caption"; tone: "secondary" }
                                Label { text: formatSpeed(speedValue); role: "mono" }

                                Label { text: "Time left"; role: "caption"; tone: "secondary" }
                                Label { text: formatEta(etaValue); role: "mono" }

                                Label { text: "Resume capability"; role: "caption"; tone: "secondary" }
                                Label {
                                    text: task && task.resumeWarning.length > 0 ? "Limited" : "Yes"
                                    role: "caption"
                                    tone: task && task.resumeWarning.length > 0 ? "warning" : "success"
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                value: progressRatio
                                indeterminate: bytesTotal <= 0 && statusText === "Active"
                                fillColor: statusText === "Error" ? Core.Colors.danger : Core.Colors.success
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        subtle: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            Label {
                                text: {
                                    if (!task) return "Segments: 0"
                                    const configured = task.segments()
                                    const active = task.effectiveSegments()
                                    return active !== configured
                                            ? ("Segments: " + configured + " (" + active + " active)")
                                            : ("Segments: " + configured)
                                }
                                role: "caption"
                                tone: "secondary"
                            }

                            Label {
                                text: "Start positions and download progress by connections"
                                role: "caption"
                                tone: "secondary"
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 18
                                radius: 4
                                color: Core.Colors.panelRaised
                                border.width: 1
                                border.color: Core.Colors.border

                                Repeater {
                                    model: task ? task.effectiveSegments() : 0

                                    delegate: Rectangle {
                                        readonly property real segTotal: task ? Math.max(1, task.segmentTotal(index)) : 1
                                        readonly property real segDone: task ? Math.max(0, task.segmentDownloaded(index)) : 0
                                        readonly property real segRatio: Math.max(0, Math.min(1, segDone / segTotal))
                                        readonly property real segWidth: parent.width / Math.max(1, (task ? task.effectiveSegments() : 1))

                                        x: index * segWidth
                                        y: 0
                                        width: segWidth
                                        height: parent.height
                                        color: "transparent"

                                        Rectangle {
                                            x: 0
                                            y: 0
                                            width: parent.width * segRatio
                                            height: parent.height
                                            color: Core.Colors.accent
                                        }

                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 1
                                            color: Core.Colors.border
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Core.Colors.border
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label { text: "N."; role: "caption"; tone: "secondary"; Layout.preferredWidth: 40 }
                                Label { text: "Downloaded"; role: "caption"; tone: "secondary"; Layout.preferredWidth: 160 }
                                Label { text: "Info"; role: "caption"; tone: "secondary"; Layout.fillWidth: true }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: task ? task.effectiveSegments() : 0
                                spacing: 4

                                delegate: Rectangle {
                                    required property int index
                                    readonly property string segState: task ? task.segmentState(index) : "Waiting"
                                    readonly property real segBytes: task ? task.segmentDownloaded(index) : 0

                                    width: ListView.view.width
                                    height: 30
                                    radius: 6
                                    color: index % 2 === 0 ? Core.Colors.panel : Core.Colors.panelAlt

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 8

                                        Label {
                                            text: String(index + 1)
                                            role: "mono"
                                            Layout.preferredWidth: 40
                                        }

                                        Label {
                                            text: formatBytes(segBytes)
                                            role: "mono"
                                            Layout.preferredWidth: 160
                                        }

                                        Label {
                                            text: segState
                                            role: "caption"
                                            tone: segState === "Receiving Data"
                                                  ? "accent"
                                                  : (segState === "Error"
                                                     ? "danger"
                                                     : "secondary")
                                            Layout.fillWidth: true
                                        }
                                    }
                                }

                                QQC2.ScrollBar.vertical: QQC2.ScrollBar { }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Card {
                        Layout.fillWidth: true

                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 8

                            Label { text: "Task speed cap (MB/s)"; role: "caption"; tone: "secondary" }

                            SpinBox {
                                id: capSpin
                                from: 0
                                to: 4096
                                value: row >= 0 ? Math.round(downloadManager.taskMaxSpeed(row) / (1024 * 1024)) : 0
                            }

                            Label { text: "Global cap"; role: "caption"; tone: "secondary" }
                            Label {
                                text: downloadManager.globalMaxSpeed > 0 ? formatSpeed(downloadManager.globalMaxSpeed) : "Unlimited"
                                role: "mono"
                            }

                            Label { text: "Queue"; role: "caption"; tone: "secondary" }
                            Label { text: queueName; role: "caption" }

                            Label { text: "Queue cap"; role: "caption"; tone: "secondary" }
                            Label {
                                text: queueName.length > 0 && downloadManager.queueMaxSpeed(queueName) > 0
                                      ? formatSpeed(downloadManager.queueMaxSpeed(queueName))
                                      : "Unlimited"
                                role: "mono"
                            }

                            Item { Layout.fillWidth: true }
                            RowLayout {
                                spacing: 8

                                Button {
                                    text: "Apply"
                                    onClicked: {
                                        if (row >= 0) {
                                            root.setSpeedCapRequested(row, capSpin.value * 1024 * 1024)
                                        }
                                    }
                                }

                                Button {
                                    text: "Unlimited"
                                    variant: "secondary"
                                    onClicked: {
                                        capSpin.value = 0
                                        if (row >= 0) {
                                            root.setSpeedCapRequested(row, 0)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Card {
                        Layout.fillWidth: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            Switch {
                                text: "Open file when completed"
                                checked: task ? task.postOpenFile : false
                                onToggled: if (task) task.postOpenFile = checked
                            }

                            Switch {
                                text: "Show in folder when completed"
                                checked: task ? task.postRevealFolder : false
                                onToggled: if (task) task.postRevealFolder = checked
                            }

                            Switch {
                                text: "Extract after completion"
                                checked: task ? task.postExtract : false
                                onToggled: if (task) task.postExtract = checked
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "Post-completion script"
                                    role: "caption"
                                    tone: "secondary"
                                }

                                TextField {
                                    Layout.fillWidth: true
                                    text: task ? task.postScript : ""
                                    placeholderText: "Optional script command"
                                    onEditingFinished: if (task) task.postScript = text
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    text: "Retry max"
                                    role: "caption"
                                    tone: "secondary"
                                }

                                SpinBox {
                                    from: -1
                                    to: 50
                                    value: task ? task.retryMax : -1
                                    onValueModified: if (task) task.retryMax = value
                                }

                                Label {
                                    text: "Retry delay (sec)"
                                    role: "caption"
                                    tone: "secondary"
                                }

                                SpinBox {
                                    from: -1
                                    to: 3600
                                    value: task ? task.retryDelaySec : -1
                                    onValueModified: if (task) task.retryDelaySec = value
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        subtle: true

                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 6

                            Label { text: "User-Agent"; role: "caption"; tone: "secondary" }
                            Label { text: task ? task.userAgent : ""; role: "caption" }

                            Label { text: "Proxy"; role: "caption"; tone: "secondary" }
                            Label {
                                text: task
                                      ? (task.proxyHost.length > 0 ? task.proxyHost + ":" + task.proxyPort : "None")
                                      : ""
                                role: "caption"
                            }

                            Label { text: "SSL Policy"; role: "caption"; tone: "secondary" }
                            Label {
                                text: task && task.allowInsecureSsl ? "Allow insecure" : "Strict"
                                role: "caption"
                                tone: task && task.allowInsecureSsl ? "warning" : "success"
                            }

                            Label { text: "Checksum"; role: "caption"; tone: "secondary" }
                            Label {
                                text: task ? (task.checksumState + (task.checksumAlgorithm.length > 0 ? " (" + task.checksumAlgorithm + ")" : "")) : ""
                                role: "caption"
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Core.Colors.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: statusText === "Active" ? "Pause" : "Resume"
                enabled: row >= 0 && (statusText === "Active" || statusText === "Paused")
                onClicked: root.pauseResumeRequested(row)
            }

            Button {
                text: "Retry"
                variant: "secondary"
                enabled: row >= 0
                onClicked: root.retryRequested(row)
            }

            Button {
                text: "Cancel"
                variant: "danger"
                enabled: row >= 0 && (statusText === "Active" || statusText === "Paused" || statusText === "Queued")
                onClicked: root.cancelRequested(row)
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Open"
                variant: "secondary"
                enabled: row >= 0 && statusText === "Done"
                onClicked: root.openRequested(row)
            }

            Button {
                text: "Show in Folder"
                variant: "secondary"
                enabled: row >= 0
                onClicked: root.revealRequested(row)
            }

            Button {
                text: "Verify"
                variant: "secondary"
                enabled: row >= 0
                onClicked: root.verifyRequested(row)
            }

            Button {
                text: "Remove"
                variant: "danger"
                enabled: row >= 0
                onClicked: root.removeRequested(row)
            }

            Button {
                text: "Close"
                variant: "ghost"
                onClicked: root.close()
            }
        }
    }
}
