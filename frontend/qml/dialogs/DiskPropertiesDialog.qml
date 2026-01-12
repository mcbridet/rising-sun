import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Dialog for viewing/editing disk image properties
// Based on SunPCi disk format from analysis/01-virtual-disk-format.md
Dialog {
    id: diskPropertiesDialog
    title: "Disk Properties"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 480
    height: Math.min(450, Screen.height - 100)

    // Properties to display (set before opening)
    property string diskPath: ""
    property int diskSizeMb: 0
    property int revision: 2
    property int cylinders: 0
    property int heads: 0
    property int sectorsPerTrack: 0
    property bool isBootable: false

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        width: parent.width
        spacing: 16

        // File info
        GroupBox {
            title: "File Information"
            Layout.fillWidth: true

            GridLayout {
                columns: 2
                rowSpacing: 8
                columnSpacing: 16

                Label { text: "Path:"; font.bold: true }
                Label { 
                    text: diskPropertiesDialog.diskPath
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Label { text: "Size:"; font.bold: true }
                Label { text: diskPropertiesDialog.diskSizeMb + " MB" }

                Label { text: "Revision:"; font.bold: true }
                Label { text: diskPropertiesDialog.revision.toString() }
            }
        }

        // Geometry
        GroupBox {
            title: "Disk Geometry (CHS)"
            Layout.fillWidth: true

            GridLayout {
                columns: 2
                rowSpacing: 8
                columnSpacing: 16

                Label { text: "Cylinders:"; font.bold: true }
                Label { text: diskPropertiesDialog.cylinders.toString() }

                Label { text: "Heads:"; font.bold: true }
                Label { text: diskPropertiesDialog.heads.toString() }

                Label { text: "Sectors/Track:"; font.bold: true }
                Label { text: diskPropertiesDialog.sectorsPerTrack.toString() }

                Label { text: "Sector Size:"; font.bold: true }
                Label { text: "512 bytes" }
            }
        }

        // Status
        GroupBox {
            title: "Status"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: diskPropertiesDialog.isBootable ? "#88cc88" : "#888888"
                    }
                    Label { text: diskPropertiesDialog.isBootable ? "Bootable" : "Not Bootable" }
                }
            }
        }

        // Drive assignment
        GroupBox {
            title: "Drive Assignment"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: "Assign to drive:" }

                ComboBox {
                    id: driveLetterCombo
                    model: ["C:", "D:", "E:", "F:"]
                    currentIndex: 0
                }

                Item { Layout.fillWidth: true }

                CheckBox {
                    id: primaryCheck
                    text: "Primary disk"
                    checked: true
                }
            }
        }
    }
    }  // ScrollView
}
