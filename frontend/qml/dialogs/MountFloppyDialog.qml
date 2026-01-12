import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

// Dialog for mounting a floppy disk image
// Based on SunPCI floppy analysis from analysis/10-floppy.md
Dialog {
    id: mountFloppyDialog
    title: "Mount Floppy Image"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 480
    height: Math.min(550, Screen.height - 100)

    property string selectedFloppyPath: ""
    property bool isMounted: false
    property int driveNumber: 0  // 0 = A:, 1 = B:

    signal floppyMounted(string path, int drive)
    signal floppyEjected(int drive)

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        width: parent.width
        spacing: 16

        // Drive selection
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Label { text: "Drive:" }

            ComboBox {
                id: driveCombo
                model: ["A: (Drive 0)", "B: (Drive 1)"]
                currentIndex: mountFloppyDialog.driveNumber
                onCurrentIndexChanged: mountFloppyDialog.driveNumber = currentIndex
            }
        }

        // Current status
        GroupBox {
            title: "Floppy Status"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 16

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: mountFloppyDialog.isMounted ? "#88cc88" : "#888888"
                }

                Text {
                    text: mountFloppyDialog.isMounted ?
                          "Mounted: " + mountFloppyDialog.selectedFloppyPath :
                          "No floppy image mounted"
                    color: palette.text
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Button {
                    text: "Eject"
                    enabled: mountFloppyDialog.isMounted
                    onClicked: {
                        mountFloppyDialog.selectedFloppyPath = ""
                        mountFloppyDialog.isMounted = false
                        floppyEjected(mountFloppyDialog.driveNumber)
                    }
                }
            }
        }

        // Floppy image selection
        GroupBox {
            title: "Select Floppy Image"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: floppyPathField
                        Layout.fillWidth: true
                        placeholderText: "Path to floppy image..."
                        text: mountFloppyDialog.selectedFloppyPath
                        readOnly: true
                    }

                    Button {
                        text: "Browse..."
                        onClicked: floppyFileDialog.open()
                    }
                }

                // Floppy format info
                Text {
                    text: "Supported formats: Raw sector images (.img, .ima, .flp, .vfd)"
                    font.pixelSize: 11
                    color: palette.text
                    opacity: 0.6
                }
            }
        }

        // Create new floppy image
        GroupBox {
            title: "Create New Floppy Image"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    spacing: 16

                    Label { text: "Format:" }

                    ComboBox {
                        id: formatCombo
                        model: ListModel {
                            // Standard formats from analysis/10-floppy.md
                            ListElement { text: "1.44 MB (3.5\" HD)"; cylinders: 80; heads: 2; sectors: 18; size: 1474560 }
                            ListElement { text: "720 KB (3.5\" DD)"; cylinders: 80; heads: 2; sectors: 9; size: 737280 }
                            ListElement { text: "1.2 MB (5.25\" HD)"; cylinders: 80; heads: 2; sectors: 15; size: 1228800 }
                            ListElement { text: "360 KB (5.25\" DD)"; cylinders: 40; heads: 2; sectors: 9; size: 368640 }
                            ListElement { text: "2.88 MB (3.5\" ED)"; cylinders: 80; heads: 2; sectors: 36; size: 2949120 }
                        }
                        textRole: "text"
                        currentIndex: 0
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Create..."
                        onClicked: createFloppyDialog.open()
                    }
                }

                // Geometry display
                Text {
                    property var fmt: formatCombo.model.get(formatCombo.currentIndex)
                    text: fmt ? "Geometry: " + fmt.cylinders + " cylinders × " + 
                                fmt.heads + " heads × " + fmt.sectors + " sectors/track" : ""
                    font.pixelSize: 11
                    color: palette.text
                    opacity: 0.6
                }
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
                    id: writeProtectCheck
                    text: "Write protect"
                    checked: false
                }

                CheckBox {
                    id: autoDetectFormatCheck
                    text: "Auto-detect format from file size"
                    checked: true
                }
            }
        }
    }
    }  // ScrollView

    FileDialog {
        id: floppyFileDialog
        title: "Select Floppy Image"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Floppy Images (*.img *.ima *.flp *.vfd)", "All Files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)

        onAccepted: {
            let path = selectedFile.toString().replace("file://", "")
            floppyPathField.text = path
            mountFloppyDialog.selectedFloppyPath = path
        }
    }

    // Create new floppy dialog
    Dialog {
        id: createFloppyDialog
        title: "Create Floppy Image"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 350
        anchors.centerIn: parent

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: newFloppyPathField
                    Layout.fillWidth: true
                    placeholderText: "floppy.img"
                }

                Button {
                    text: "..."
                    onClicked: saveFloppyDialog.open()
                }
            }

            Text {
                property var fmt: formatCombo.model.get(formatCombo.currentIndex)
                text: fmt ? "Will create a blank " + fmt.text + " image\n(" + fmt.size + " bytes)" : ""
                font.pixelSize: 11
                color: palette.text
            }
        }

        FileDialog {
            id: saveFloppyDialog
            title: "Save Floppy Image As"
            fileMode: FileDialog.SaveFile
            nameFilters: ["Floppy Images (*.img)", "All Files (*)"]

            onAccepted: {
                newFloppyPathField.text = selectedFile.toString().replace("file://", "")
            }
        }

        onAccepted: {
            // TODO: Create the floppy image file
            floppyPathField.text = newFloppyPathField.text
            mountFloppyDialog.selectedFloppyPath = newFloppyPathField.text
        }
    }

    onAccepted: {
        if (selectedFloppyPath !== "") {
            isMounted = true
            floppyMounted(selectedFloppyPath, driveNumber)
        }
    }
}
