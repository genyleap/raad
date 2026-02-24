import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 980
    height: 640
    visible: true
    title: "Raad"

    Rectangle {
        anchors.fill: parent
        color: "#111827"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: "Raad Backend Console"
            font.pixelSize: 24
            color: "#f9fafb"
        }

        Label {
            text: "UI is under construction. Backend is active."
            color: "#d1d5db"
        }

        RowLayout {
            spacing: 16

            Label { text: "Active: " + downloadManager.activeCount; color: "#93c5fd" }
            Label { text: "Queued: " + downloadManager.queuedCount; color: "#93c5fd" }
            Label { text: "Completed: " + downloadManager.completedCount; color: "#93c5fd" }
            Label { text: "Speed: " + downloadManager.totalSpeed + " B/s"; color: "#93c5fd" }
        }
    }
}
