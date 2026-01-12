import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

// Dialog for creating a new virtual disk image
// Based on SunPCI makedisk parameters from analysis/01-virtual-disk-format.md
Dialog {
    id: createDiskDialog
    title: "Create Virtual Disk"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 550
    height: Math.min(550, Screen.height - 100)
    
    property string selectedPath: ""
    
    signal diskCreated(string path, int sizeMb, int revision)

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        width: parent.width
        spacing: 16

        // Disk name/path selection
        GroupBox {
            title: "Disk File"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 8

                TextField {
                    id: diskPathField
                    Layout.fillWidth: true
                    placeholderText: "~/pc/C.diskimage"
                    text: createDiskDialog.selectedPath
                    readOnly: true
                }

                Button {
                    text: "Browse..."
                    onClicked: saveFileDialog.open()
                }
            }
        }

        // Disk size selection
        GroupBox {
            title: "Disk Size"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    spacing: 16

                    Label {
                        text: "Size:"
                    }

                    SpinBox {
                        id: diskSizeSpinBox
                        from: 10      // Minimum 10 MB (from create_disk)
                        to: 8000      // Maximum 8000 MB (8 GB limit from BIOS)
                        value: 512
                        stepSize: 100
                        editable: true

                        textFromValue: function(value) {
                            return value + " MB"
                        }
                        valueFromText: function(text) {
                            return parseInt(text)
                        }
                    }
                }

                // Quick size presets
                RowLayout {
                    spacing: 8
                    
                    Label {
                        text: "Presets:"
                        opacity: 0.7
                    }

                    Button {
                        text: "256 MB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 256
                    }
                    Button {
                        text: "512 MB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 512
                    }
                    Button {
                        text: "1 GB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 1024
                    }
                    Button {
                        text: "2 GB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 2048
                    }
                    Button {
                        text: "4 GB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 4096
                    }
                    Button {
                        text: "8 GB"
                        flat: true
                        onClicked: diskSizeSpinBox.value = 8000
                    }
                }

                // Info about geometry
                Text {
                    text: "Note: Disk geometry is automatically calculated.\n" +
                          "CHS addressing limited to 1024 cylinders."
                    font.pixelSize: 11
                    color: palette.text
                    opacity: 0.6
                }
            }
        }

        // Disk revision (format version)
        GroupBox {
            title: "Disk Format"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ComboBox {
                    id: revisionCombo
                    model: [
                        { text: "Revision 2 (Recommended)", value: 2 },
                        { text: "Revision 1 (Legacy)", value: 1 }
                    ]
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: 0
                }

                Text {
                    text: "Revision 2 supports larger disks and improved performance."
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
                    id: bootableCheck
                    text: "Make bootable (install DOS boot sector)"
                    checked: true
                }

                CheckBox {
                    id: formatCheck
                    text: "Format disk (FAT file system)"
                    checked: true
                }
            }
        }
    }
    }  // ScrollView

    FileDialog {
        id: saveFileDialog
        title: "Save Disk Image As"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Disk Images (*.diskimage)", "All Files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        
        onAccepted: {
            createDiskDialog.selectedPath = selectedFile.toString().replace("file://", "")
            diskPathField.text = createDiskDialog.selectedPath
        }
    }

    onAccepted: {
        if (selectedPath !== "") {
            diskCreated(selectedPath, diskSizeSpinBox.value, revisionCombo.currentValue)
        }
    }
}
