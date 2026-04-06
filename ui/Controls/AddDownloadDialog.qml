/*!
    \file        AddDownloadDialog.qml
    \brief       Implements the AddDownloadDialog QML component for RAAD.
    \details     This file contains the AddDownloadDialog user interface component used by the RAAD desktop application.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

import QtQuick
import QtQuick.Layouts
import "../Core" as Core

Card {
    id: root

    property var queueModel: []
    property var categoryModel: []
    property string defaultPath: ""
    property alias urlInput: urlField

    signal addRequested(string url,
                        string path,
                        string queueName,
                        string category,
                        bool startPaused,
                        int segments,
                        bool adaptive)

    implicitHeight: 156

    function effectivePath() {
        const p = pathField.text.trim()
        return p.length > 0 ? p : root.defaultPath
    }

    onDefaultPathChanged: {
        if (pathField.text.trim().length === 0) {
            pathField.text = defaultPath
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 11

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "URL to download"
            }

            TextField {
                id: pathField
                Layout.preferredWidth: 360
                text: root.defaultPath
                placeholderText: "Destination folder"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: queueBox
                Layout.preferredWidth: 160
                model: root.queueModel
            }

            ComboBox {
                id: categoryBox
                Layout.preferredWidth: 146
                model: root.categoryModel
            }

            ComboBox {
                id: segmentBox
                Layout.preferredWidth: 114
                model: ["4", "6", "8", "12", "16", "24", "32"]
                currentIndex: 1
            }

            Switch {
                id: adaptiveSwitch
                text: "Adaptive segments"
                checked: true
            }

            Switch {
                id: pausedSwitch
                text: "Start paused"
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Add download"
                variant: "primary"
                emphasize: true
                enabled: urlField.text.trim().length > 0
                onClicked: {
                    var q = queueBox.currentText
                    if (!q || q.length === 0) {
                        q = "General"
                    }
                    var c = categoryBox.currentText
                    if (!c || c.length === 0) {
                        c = "Auto"
                    }
                    root.addRequested(urlField.text.trim(),
                                      root.effectivePath(),
                                      q,
                                      c,
                                      pausedSwitch.checked,
                                      parseInt(segmentBox.currentText),
                                      adaptiveSwitch.checked)
                    urlField.clear()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            role: "micro"
            tone: "muted"
            text: "Segment guidance: 4-8 normal, 8-16 fast links, 16-32 high-throughput CDN/SSD."
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            role: "micro"
            tone: "muted"
            wrapMode: Text.Wrap
            text: "Adaptive note: ON = segment count can change dynamically during download. OFF = segment count remains fixed to the value you set."
        }
    }
}
