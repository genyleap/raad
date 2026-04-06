/*!
    \file        DownloadListView.qml
    \brief       Implements the DownloadListView QML component for RAAD.
    \details     This file contains the DownloadListView user interface component used by the RAAD desktop application.

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

    property var model: null
    property string queueFilter: "All Queues"
    property string statusFilter: "All"
    property string categoryFilter: "All"
    property string searchText: ""

    property int selectedIndex: -1
    property real nameWidth: 320
    property real queueWidth: 110
    property real sizeWidth: 160
    property real statusWidth: 118
    property real etaWidth: 96
    property real speedWidth: 110
    property real segmentsWidth: 92
    property real categoryWidth: 98
    property real actionsWidth: 184

    readonly property int visibleCount: model && model.filteredCount
                                     ? model.filteredCount(queueFilter,
                                                           statusFilter,
                                                           categoryFilter,
                                                           searchText)
                                     : 0

    signal taskSelected(int row,
                        var taskObj,
                        string queue,
                        string category)
    signal togglePauseRequested(int row)
    signal removeRequested(int row)
    signal openRequested(int row)
    signal contextActionRequested(int row, var taskObj, string action)
    signal detailsRequested(int row,
                           var taskObj,
                           string queue,
                           string category)

    function statusPasses(status) {
        if (statusFilter === "All") return true
        if (statusFilter === "Unfinished") {
            return status !== "Done" && status !== "Canceled" && status !== "Error"
        }
        if (statusFilter === "History") {
            return status === "Done" || status === "Canceled" || status === "Error"
        }
        return status === statusFilter
    }

    function rowAccepted(queueName, status, categoryName, fileName, urlText) {
        if (queueFilter !== "All Queues" && queueName !== queueFilter) return false
        if (!statusPasses(status)) return false
        if (categoryFilter !== "All" && categoryName !== categoryFilter) return false

        const needle = searchText.trim().toLowerCase()
        if (needle.length === 0) return true

        const file = (fileName || "").toLowerCase()
        const url = (urlText || "").toLowerCase()
        return file.indexOf(needle) >= 0 || url.indexOf(needle) >= 0
    }

    ListView {
        id: view
        anchors.fill: parent
        model: root.model
        spacing: 8
        clip: true
        cacheBuffer: 1280
        boundsBehavior: Flickable.StopAtBounds

        delegate: DownloadDelegate {
            row: index
            fileName: model.fileName
            status: model.status
            bytesReceived: model.bytesReceived
            bytesTotal: model.bytesTotal
            queueName: model.queueName
            category: model.category
            task: model.task
            nameWidth: root.nameWidth
            queueWidth: root.queueWidth
            sizeWidth: root.sizeWidth
            statusWidth: root.statusWidth
            etaWidth: root.etaWidth
            speedWidth: root.speedWidth
            segmentsWidth: root.segmentsWidth
            categoryWidth: root.categoryWidth
            actionsWidth: root.actionsWidth

            width: ListView.view.width
            selected: root.selectedIndex === row
            filterAccepted: root.rowAccepted(queueName,
                                             status,
                                             category,
                                             fileName,
                                             task ? task.url() : "")

            onSelectRequested: function(row, taskObj, queue, categoryName) {
                root.taskSelected(row, taskObj, queue, categoryName)
            }

            onPauseResumeRequested: function(row) {
                root.togglePauseRequested(row)
            }

            onRemoveRequested: function(row) {
                root.removeRequested(row)
            }

            onOpenRequested: function(row) {
                root.openRequested(row)
            }

            onContextActionRequested: function(row, taskObj, action) {
                root.contextActionRequested(row, taskObj, action)
            }

            onDetailsRequested: function(row, taskObj, queue, categoryName) {
                root.detailsRequested(row, taskObj, queue, categoryName)
            }
        }

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {
            policy: QQC2.ScrollBar.AsNeeded
        }
    }

    Label {
        anchors.centerIn: parent
        visible: root.visibleCount === 0
        text: "No downloads match current filters"
        tone: "secondary"
        role: "caption"
    }
}
