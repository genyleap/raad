/*!
    \file        DownloadDetailsWindow.qml
    \brief       Implements the DownloadDetailsWindow QML component for RAAD.
    \details     This file contains the DownloadDetailsWindow user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import "../Core" as Core

Window {
    id: root

    property int row: -1
    property var task: null
    property string queueName: ""
    property string categoryName: ""
    property bool showConnectionDetails: true

    property var speedSamples: []
    property int maxSpeedSamples: 120
    property real observedMaxSpeed: 1
    property real bytesReceived: 0
    property real bytesTotal: 0
    property int progressRevision: 0
    property int progressGridCells: 0
    property int progressGridFilled: 0

    signal pauseResumeRequested(int row)
    signal cancelRequested(int row)
    signal retryRequested(int row)
    signal openRequested(int row)
    signal revealRequested(int row)
    signal removeRequested(int row)
    signal verifyRequested(int row)
    signal setSpeedCapRequested(int row, int bytesPerSec)
    signal copyRequested(string text)

    width: 1040
    height: (tabs.currentIndex === 1 || tabs.currentIndex === 2) ? 780 : 660
    minimumWidth: 900
    minimumHeight: 580
    visible: false
    color: Core.Colors.window

    readonly property real progressRatio: bytesTotal > 0 ? Math.min(1.0, bytesReceived / bytesTotal) : 0.0
    readonly property real speedValue: task ? task.speed : 0
    readonly property int etaValue: task ? task.eta : -1
    readonly property string statusText: task ? task.stateString : ""

    title: {
        if (!task) {
            return "Download details"
        }
        return Math.round(progressRatio * 100) + "% " + baseName(task.fileName())
    }

    Component.onCompleted: {
        resetSpeedSamples()
        refreshProgressSnapshot()
    }

    onRowChanged: {
        resetSpeedSamples()
        refreshProgressSnapshot()
    }

    onTaskChanged: refreshProgressSnapshot()

    onVisibleChanged: {
        if (visible) {
            resetSpeedSamples()
            refreshProgressSnapshot()
            pushSpeedSample(speedValue)
        }
    }

    Timer {
        id: speedSampleTimer
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.pushSpeedSample(root.speedValue)
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
            return Math.floor(s) + " sec"
        }
        const m = Math.floor(s / 60)
        const sec = Math.floor(s % 60)
        if (m < 60) {
            return m + " min " + sec + " sec"
        }
        const h = Math.floor(m / 60)
        return h + " h " + (m % 60) + " min"
    }

    function stateTone() {
        if (statusText === "Error") return "danger"
        if (statusText === "Active") return "success"
        if (statusText === "Paused") return "warning"
        if (statusText === "Done") return "success"
        return "secondary"
    }

    function resetSpeedSamples() {
        speedSamples = []
        observedMaxSpeed = 1
    }

    function refreshProgressSnapshot() {
        if (row >= 0) {
            bytesReceived = Math.max(0, downloadManager.taskBytesReceived(row))
            bytesTotal = Math.max(0, downloadManager.taskBytesTotal(row))
        } else {
            bytesReceived = 0
            bytesTotal = 0
        }
        progressRevision += 1
    }

    function pushSpeedSample(value) {
        var arr = speedSamples.slice(0)
        var sample = Number(value)
        if (!isFinite(sample) || sample < 0) {
            sample = 0
        }
        arr.push(sample)
        while (arr.length > maxSpeedSamples) {
            arr.shift()
        }
        speedSamples = arr

        if (arr.length > 0) {
            observedMaxSpeed = Math.max(1, Math.max.apply(Math, arr))
        } else {
            observedMaxSpeed = 1
        }
    }

    function alphaColor(colorValue, alphaValue) {
        return Qt.rgba(colorValue.r, colorValue.g, colorValue.b, alphaValue)
    }

    Connections {
        target: task

        function onProgress(received, total) {
            root.bytesReceived = Math.max(0, Number(received))
            root.bytesTotal = Math.max(0, Number(total))
            root.progressRevision += 1
        }

        function onStateChanged() {
            root.refreshProgressSnapshot()
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Core.Colors.window }
            GradientStop { position: 1.0; color: Core.Colors.windowAlt }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.title
                role: "title"
                elide: Text.ElideRight
            }

            Rectangle {
                radius: 10
                color: Core.Colors.alpha(Core.Colors.accent, 0.16)
                border.width: 1
                border.color: {
                    if (stateTone() === "danger") return Core.Colors.danger
                    if (stateTone() === "success") return Core.Colors.success
                    if (stateTone() === "warning") return Core.Colors.warning
                    return Core.Colors.borderStrong
                }
                implicitHeight: 28
                implicitWidth: stateLabel.implicitWidth + 16

                Label {
                    id: stateLabel
                    anchors.centerIn: parent
                    text: statusText
                    role: "caption"
                    tone: stateTone()
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            value: progressRatio
            indeterminate: bytesTotal <= 0 && statusText === "Active"
            fillColor: statusText === "Error" ? Core.Colors.danger : Core.Colors.success
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: "General" }
            TabButton { text: "Progress" }
            TabButton { text: "Connections" }
            TabButton { text: "Speed limiter" }
            TabButton { text: "Completion" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Card {
                        Layout.fillWidth: true
                        Layout.preferredHeight: statusGrid.implicitHeight + 24

                        GridLayout {
                            id: statusGrid
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 3
                            columnSpacing: 10
                            rowSpacing: 7

                            Label { text: "URL"; role: "micro"; tone: "muted" }
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
                                onClicked: if (task) root.copyRequested(task.url())
                            }

                            Label { text: "Status"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: statusText === "Active" ? "Receiving data" : statusText; role: "caption"; tone: stateTone() }

                            Label { text: "File size"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: formatBytes(bytesTotal); role: "mono" }

                            Label { text: "Downloaded"; role: "micro"; tone: "muted" }
                            Label {
                                Layout.columnSpan: 2
                                text: formatBytes(bytesReceived)
                                      + (bytesTotal > 0 ? " / " + formatBytes(bytesTotal) : "")
                                      + (bytesTotal > 0 ? " (" + (progressRatio * 100).toFixed(2) + "%)" : "")
                                role: "mono"
                            }

                            Label { text: "Transfer"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: formatSpeed(speedValue); role: "mono" }

                            Label { text: "Time left"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: formatEta(etaValue); role: "mono" }

                            Label { text: "Resume"; role: "micro"; tone: "muted" }
                            Label {
                                Layout.columnSpan: 2
                                text: task && task.resumeWarning.length > 0 ? "Limited" : "Yes"
                                role: "caption"
                                tone: task && task.resumeWarning.length > 0 ? "warning" : "success"
                            }

                            Label { text: "Queue"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: queueName; role: "caption" }

                            Label { text: "Category"; role: "micro"; tone: "muted" }
                            Label { Layout.columnSpan: 2; text: categoryName; role: "caption" }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: statusText === "Active" ? "Pause" : "Resume"
                            enabled: row >= 0 && (statusText === "Active" || statusText === "Paused")
                            onClicked: root.pauseResumeRequested(row)
                        }

                        Button {
                            text: "Cancel"
                            variant: "danger"
                            enabled: row >= 0 && (statusText === "Active" || statusText === "Paused" || statusText === "Queued")
                            onClicked: root.cancelRequested(row)
                        }

                        Button {
                            text: "Retry"
                            variant: "secondary"
                            enabled: row >= 0
                            onClicked: root.retryRequested(row)
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: "Adaptive target: " + (task ? task.adaptiveTarget : 0)
                            role: "caption"
                            tone: "secondary"
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: [
                                { title: "Progress", value: (progressRatio * 100).toFixed(2) + "%", tone: "accent" },
                                { title: "Downloaded", value: formatBytes(bytesReceived), tone: "secondary" },
                                { title: "Speed", value: formatSpeed(speedValue), tone: "success" },
                                { title: "ETA", value: formatEta(etaValue), tone: "secondary" }
                            ]

                            delegate: Card {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 72
                                subtle: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 1

                                    Label {
                                        text: modelData.title
                                        role: "micro"
                                        tone: "secondary"
                                    }

                                    Label {
                                        text: modelData.value
                                        role: "caption"
                                        tone: modelData.tone
                                    }
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 220

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "Download speed (live)"
                                    role: "caption"
                                    tone: "secondary"
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: "Current " + formatSpeed(speedValue)
                                    role: "micro"
                                    tone: "secondary"
                                }

                                Label {
                                    text: "Peak " + formatSpeed(observedMaxSpeed)
                                    role: "micro"
                                    tone: "secondary"
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 8
                                color: Core.Colors.panel
                                border.width: 1
                                border.color: Core.Colors.border

                                Canvas {
                                    id: speedChartCanvas
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    antialiasing: true

                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()

                                        var w = width
                                        var h = height
                                        if (w <= 2 || h <= 2) {
                                            return
                                        }

                                        var pad = 12
                                        var chartW = Math.max(1, w - pad * 2)
                                        var chartH = Math.max(1, h - pad * 2)

                                        ctx.strokeStyle = root.alphaColor(Core.Colors.border, 0.6)
                                        ctx.lineWidth = 1
                                        for (var g = 0; g <= 4; ++g) {
                                            var gy = pad + (chartH * g / 4)
                                            ctx.beginPath()
                                            ctx.moveTo(pad, gy)
                                            ctx.lineTo(w - pad, gy)
                                            ctx.stroke()
                                        }

                                        var samples = root.speedSamples
                                        if (!samples || samples.length === 0) {
                                            ctx.fillStyle = root.alphaColor(Core.Colors.textMuted, 0.9)
                                            ctx.font = "12px sans-serif"
                                            ctx.fillText("Waiting for speed samples...", pad + 6, h / 2)
                                            return
                                        }

                                        var maxSpeed = Math.max(1, root.observedMaxSpeed)
                                        var step = chartW / Math.max(1, samples.length - 1)

                                        ctx.beginPath()
                                        for (var i = 0; i < samples.length; ++i) {
                                            var px = pad + i * step
                                            var py = pad + chartH - (samples[i] / maxSpeed) * chartH
                                            if (i === 0) {
                                                ctx.moveTo(px, py)
                                            } else {
                                                ctx.lineTo(px, py)
                                            }
                                        }

                                        ctx.lineWidth = 2
                                        ctx.strokeStyle = Core.Colors.accent
                                        ctx.stroke()

                                        ctx.lineTo(pad + chartW, pad + chartH)
                                        ctx.lineTo(pad, pad + chartH)
                                        ctx.closePath()
                                        ctx.fillStyle = root.alphaColor(Core.Colors.accent, 0.22)
                                        ctx.fill()
                                    }

                                    Connections {
                                        target: root
                                        function onSpeedSamplesChanged() { speedChartCanvas.requestPaint() }
                                        function onObservedMaxSpeedChanged() { speedChartCanvas.requestPaint() }
                                    }

                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 220
                        subtle: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "Segmented progress map"
                                    role: "caption"
                                    tone: "secondary"
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: progressGridFilled + " / " + progressGridCells + " cells"
                                    role: "micro"
                                    tone: "secondary"
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 8
                                color: Core.Colors.panel
                                border.width: 1
                                border.color: Core.Colors.border

                                Canvas {
                                    id: progressCanvas
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    antialiasing: true

                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()

                                        var w = width
                                        var h = height
                                        if (w <= 2 || h <= 2) {
                                            return
                                        }

                                        var cell = 9
                                        var gap = 3
                                        var cols = Math.max(1, Math.floor((w + gap) / (cell + gap)))
                                        var rows = Math.max(1, Math.floor((h + gap) / (cell + gap)))
                                        var total = cols * rows
                                        var filled = Math.max(0, Math.min(total, Math.floor(total * root.progressRatio)))

                                        root.progressGridCells = total
                                        root.progressGridFilled = filled

                                        for (var i = 0; i < total; ++i) {
                                            var c = i % cols
                                            var r = Math.floor(i / cols)
                                            var x = c * (cell + gap)
                                            var y = r * (cell + gap)
                                            ctx.fillStyle = i < filled
                                                    ? root.alphaColor(Core.Colors.accent, 0.85)
                                                    : root.alphaColor(Core.Colors.panelRaised, 0.9)
                                            ctx.fillRect(x, y, cell, cell)
                                        }
                                    }

                                    Connections {
                                        target: root
                                        function onProgressRatioChanged() { progressCanvas.requestPaint() }
                                    }

                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: {
                                if (!task) return "Connection segments: 0"
                                const configured = task.segments()
                                const active = task.effectiveSegments()
                                return active !== configured
                                        ? ("Connection segments: " + configured + " (" + active + " active)")
                                        : ("Connection segments: " + configured)
                            }
                            role: "caption"
                            tone: "secondary"
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: root.showConnectionDetails ? "Hide table" : "Show table"
                            variant: "secondary"
                            compact: true
                            onClicked: root.showConnectionDetails = !root.showConnectionDetails
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        subtle: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Start positions and progress by connections"
                                role: "caption"
                                tone: "secondary"
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 20
                                radius: 6
                                color: Core.Colors.panelRaised
                                border.width: 1
                                border.color: Core.Colors.border

                                Repeater {
                                    model: task ? task.effectiveSegments() : 0

                                    delegate: Rectangle {
                                        readonly property real segTotal: {
                                            const tick = root.progressRevision
                                            return task ? Math.max(1, task.segmentTotal(index)) : 1
                                        }
                                        readonly property real segDone: {
                                            const tick = root.progressRevision
                                            return task ? Math.max(0, task.segmentDownloaded(index)) : 0
                                        }
                                        readonly property real segRatio: Math.max(0, Math.min(1, segDone / segTotal))
                                        readonly property real segWidth: parent.width / Math.max(1, (task ? task.effectiveSegments() : 1))

                                        x: index * segWidth
                                        y: 0
                                        width: segWidth
                                        height: parent.height
                                        color: "transparent"

                                        Rectangle {
                                            x: 0
                                            y: 1
                                            width: Math.max(0, parent.width * segRatio)
                                            height: parent.height - 2
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
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.showConnectionDetails

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 0
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                height: 30
                                radius: 10
                                color: Core.Colors.panelAlt

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8

                                    Label { text: "N"; role: "micro"; tone: "muted"; Layout.preferredWidth: 40 }
                                    Label { text: "Downloaded"; role: "micro"; tone: "muted"; Layout.preferredWidth: 170 }
                                    Label { text: "Info"; role: "micro"; tone: "muted"; Layout.fillWidth: true }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: task ? task.effectiveSegments() : 0

                                delegate: Rectangle {
                                    required property int index
                                    readonly property string segState: {
                                        const tick = root.progressRevision
                                        return task ? task.segmentState(index) : "Waiting"
                                    }
                                    readonly property real segBytes: {
                                        const tick = root.progressRevision
                                        return task ? task.segmentDownloaded(index) : 0
                                    }

                                    width: ListView.view.width
                                    height: 30
                                    color: index % 2 === 0 ? Core.Colors.panel : Core.Colors.panelAlt

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 8

                                        Label {
                                            text: String(index + 1)
                                            role: "mono"
                                            tone: "secondary"
                                            Layout.preferredWidth: 40
                                        }

                                        Label {
                                            text: formatBytes(segBytes)
                                            role: "mono"
                                            tone: "secondary"
                                            Layout.preferredWidth: 170
                                        }

                                        Label {
                                            text: segState
                                            role: "caption"
                                            tone: segState === "Error"
                                                  ? "danger"
                                                  : (segState === "Receiving Data" ? "accent" : "secondary")
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
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
                        Layout.preferredHeight: speedGrid.implicitHeight + 24

                        GridLayout {
                            id: speedGrid
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 8

                            Label { text: "Task speed cap (MB/s)"; role: "caption"; tone: "secondary" }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                SpinBox {
                                    id: capSpin
                                    from: 0
                                    to: 4096
                                    value: row >= 0 ? Math.round(downloadManager.taskMaxSpeed(row) / (1024 * 1024)) : 0
                                }

                                Button {
                                    text: "Apply"
                                    onClicked: if (row >= 0) root.setSpeedCapRequested(row, capSpin.value * 1024 * 1024)
                                }

                                Button {
                                    text: "Unlimited"
                                    variant: "secondary"
                                    onClicked: {
                                        capSpin.value = 0
                                        if (row >= 0) root.setSpeedCapRequested(row, 0)
                                    }
                                }
                            }

                            Label { text: "Current speed"; role: "caption"; tone: "secondary" }
                            Label { text: formatSpeed(speedValue); role: "mono" }

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
                        Layout.preferredHeight: completionColumn.implicitHeight + 24

                        ColumnLayout {
                            id: completionColumn
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
                                spacing: 8

                                Label { text: "Post script"; role: "caption"; tone: "secondary" }
                                TextField {
                                    Layout.fillWidth: true
                                    text: task ? task.postScript : ""
                                    placeholderText: "Optional command"
                                    onEditingFinished: if (task) task.postScript = text
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label { text: "Retry max"; role: "caption"; tone: "secondary" }
                                SpinBox {
                                    from: -1
                                    to: 50
                                    value: task ? task.retryMax : -1
                                    onValueModified: if (task) task.retryMax = value
                                }

                                Label { text: "Retry delay (sec)"; role: "caption"; tone: "secondary" }
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
                        Layout.preferredHeight: transportGrid.implicitHeight + 24
                        subtle: true

                        GridLayout {
                            id: transportGrid
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 6

                            Label { text: "User-Agent"; role: "caption"; tone: "secondary" }
                            Label { text: task ? task.userAgent : ""; role: "caption" }

                            Label { text: "Proxy"; role: "caption"; tone: "secondary" }
                            Label {
                                text: task ? (task.proxyHost.length > 0 ? task.proxyHost + ":" + task.proxyPort : "None") : ""
                                role: "caption"
                            }

                            Label { text: "SSL policy"; role: "caption"; tone: "secondary" }
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
                variant: "ghost"
                enabled: row >= 0 && statusText === "Done"
                onClicked: root.openRequested(row)
            }

            Button {
                text: "Show in folder"
                variant: "ghost"
                enabled: row >= 0
                onClicked: root.revealRequested(row)
            }

            Button {
                text: "Verify"
                variant: "ghost"
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
                variant: "secondary"
                onClicked: root.close()
            }
        }
    }
}
