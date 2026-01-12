import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

// Dialog for mounting an ISO file as a CD-ROM
// Based on SunPCI CD-ROM analysis from analysis/09-cdrom.md
Dialog {
    id: mountIsoDialog
    title: "Mount CD-ROM Image"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 500
    height: Math.min(500, Screen.height - 100)

    property string selectedIsoPath: ""
    property bool isMounted: false

    signal isoMounted(string path)
    signal isoEjected()

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        width: parent.width
        spacing: 16

        // Current status
        GroupBox {
            title: "CD-ROM Status"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 16

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: mountIsoDialog.isMounted ? "#88cc88" : "#888888"
                }

                Text {
                    text: mountIsoDialog.isMounted ? 
                          "Mounted: " + mountIsoDialog.selectedIsoPath :
                          "No disc mounted"
                    color: palette.text
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Button {
                    text: "Eject"
                    enabled: mountIsoDialog.isMounted
                    onClicked: {
                        mountIsoDialog.selectedIsoPath = ""
                        mountIsoDialog.isMounted = false
                        isoEjected()
                    }
                }
            }
        }

        // ISO file selection
        GroupBox {
            title: "Select ISO Image"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: isoPathField
                        Layout.fillWidth: true
                        placeholderText: "Path to ISO file..."
                        text: mountIsoDialog.selectedIsoPath
                        readOnly: true
                    }

                    Button {
                        text: "Browse..."
                        onClicked: isoFileDialog.open()
                    }
                }

                // Recent ISOs list (placeholder)
                Text {
                    text: "Recent images:"
                    font.bold: true
                    color: palette.text
                }

                ListView {
                    id: recentIsosList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    clip: true
                    
                    model: ListModel {
                        // Would be populated from settings
                        ListElement { path: "~/isos/win98se.iso" }
                        ListElement { path: "~/isos/dos622.iso" }
                    }

                    delegate: ItemDelegate {
                        width: recentIsosList.width
                        height: 24
                        text: model.path
                        font.pixelSize: 11
                        onClicked: {
                            isoPathField.text = model.path
                            mountIsoDialog.selectedIsoPath = model.path
                        }
                    }
                }
            }
        }

        // CD-ROM info (shown when mounted)
        GroupBox {
            title: "Disc Information"
            Layout.fillWidth: true
            visible: mountIsoDialog.isMounted

            GridLayout {
                columns: 2
                rowSpacing: 8
                columnSpacing: 16

                Label { text: "Format:"; font.bold: true }
                Label { text: "ISO 9660 + Joliet" }

                Label { text: "Sector Size:"; font.bold: true }
                Label { text: "2048 bytes" }

                Label { text: "Volume Label:"; font.bold: true }
                Label { text: "(detected from ISO)" }
            }
        }

        // Options
        GroupBox {
            title: "Options"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                CheckBox {
                    id: autoMountCheck
                    text: "Mount automatically when session starts"
                    checked: true
                }

                CheckBox {
                    id: bootFromCdCheck
                    text: "Boot from CD-ROM (El Torito)"
                    checked: false
                }

                Text {
                    text: "Note: CD audio playback is not supported."
                    font.pixelSize: 11
                    color: palette.text
                    opacity: 0.6
                }
            }
        }
    }
    }  // ScrollView

    FileDialog {
        id: isoFileDialog
        title: "Select ISO Image"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ISO Images (*.iso *.ISO)", "All Files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)

        onAccepted: {
            let path = selectedFile.toString().replace("file://", "")
            isoPathField.text = path
            mountIsoDialog.selectedIsoPath = path
        }
    }

    onAccepted: {
        if (selectedIsoPath !== "") {
            isMounted = true
            isoMounted(selectedIsoPath)
        }
    }
}
