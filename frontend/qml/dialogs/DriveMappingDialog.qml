import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

// Dialog for managing host directory → guest drive letter mappings
// Based on SunPCI filesystem redirection from analysis/05-filesystem-redirection.md
// Equivalent to "sunpcnet use X: /path" functionality
Dialog {
    id: driveMappingDialog
    title: "Drive Mappings"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 550
    height: Math.min(500, Screen.height - 100)

    // Model for drive mappings
    ListModel {
        id: driveMappingsModel
        // Default mappings like original SunPCI
        ListElement { driveLetter: "F:"; hostPath: "/opt/SUNWspci"; description: "SunPCI Installation"; enabled: true }
        ListElement { driveLetter: "H:"; hostPath: "~"; description: "Home Directory"; enabled: true }
        ListElement { driveLetter: "R:"; hostPath: "/"; description: "Root Filesystem"; enabled: false }
    }

    signal mappingsApplied(var mappings)

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // Info text
        Text {
            Layout.fillWidth: true
            text: "Map host directories to DOS/Windows drive letters.\n" +
                  "These drives appear as network drives in the guest."
            font.pixelSize: 12
            color: palette.text
            wrapMode: Text.WordWrap
        }

        // Drive mappings list
        GroupBox {
            title: "Current Mappings"
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        text: "Drive"
                        font.bold: true
                        Layout.preferredWidth: 50
                        color: palette.text
                    }
                    Text {
                        text: "Host Path"
                        font.bold: true
                        Layout.fillWidth: true
                        color: palette.text
                    }
                    Text {
                        text: "Enabled"
                        font.bold: true
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignHCenter
                        color: palette.text
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: palette.mid
                }

                // Mappings list
                ListView {
                    id: mappingsListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: driveMappingsModel
                    clip: true
                    spacing: 4

                    delegate: Rectangle {
                        width: mappingsListView.width
                        height: 36
                        color: ListView.isCurrentItem ? palette.highlight : "transparent"
                        radius: 4

                        MouseArea {
                            anchors.fill: parent
                            onClicked: mappingsListView.currentIndex = index
                            onDoubleClicked: editMappingDialog.openForEdit(index)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 0

                            Text {
                                text: model.driveLetter
                                font.family: "monospace"
                                font.bold: true
                                Layout.preferredWidth: 42
                                color: parent.parent.ListView.isCurrentItem ? palette.highlightedText : palette.text
                            }

                            Text {
                                text: model.hostPath
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                color: parent.parent.ListView.isCurrentItem ? palette.highlightedText : palette.text
                                opacity: model.enabled ? 1.0 : 0.5
                            }

                            CheckBox {
                                checked: model.enabled
                                Layout.preferredWidth: 60
                                Layout.alignment: Qt.AlignHCenter
                                onCheckedChanged: {
                                    driveMappingsModel.setProperty(index, "enabled", checked)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Action buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: "Add..."
                icon.name: "list-add"
                onClicked: editMappingDialog.openForNew()
            }

            Button {
                text: "Edit..."
                icon.name: "document-edit"
                enabled: mappingsListView.currentIndex >= 0
                onClicked: editMappingDialog.openForEdit(mappingsListView.currentIndex)
            }

            Button {
                text: "Remove"
                icon.name: "list-remove"
                enabled: mappingsListView.currentIndex >= 0
                onClicked: {
                    driveMappingsModel.remove(mappingsListView.currentIndex)
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Restore Defaults"
                onClicked: {
                    driveMappingsModel.clear()
                    driveMappingsModel.append({ driveLetter: "F:", hostPath: "/opt/SUNWspci", description: "SunPCI Installation", enabled: true })
                    driveMappingsModel.append({ driveLetter: "H:", hostPath: "~", description: "Home Directory", enabled: true })
                    driveMappingsModel.append({ driveLetter: "R:", hostPath: "/", description: "Root Filesystem", enabled: false })
                }
            }
        }

        // Options
        GroupBox {
            title: "Options"
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 16

                CheckBox {
                    id: msDosCompatCheck
                    text: "MS-DOS compatible sharing mode (/ms)"
                    checked: false
                }

                CheckBox {
                    id: mandatoryLockCheck
                    text: "Mandatory locking (/ml)"
                    checked: false
                    enabled: msDosCompatCheck.checked
                }
            }
        }
    }

    // Dialog for editing a mapping
    Dialog {
        id: editMappingDialog
        title: editIndex >= 0 ? "Edit Drive Mapping" : "Add Drive Mapping"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400
        anchors.centerIn: parent

        property int editIndex: -1

        function openForNew() {
            editIndex = -1
            driveLetterField.text = "G:"
            hostPathField.text = ""
            descriptionField.text = ""
            open()
        }

        function openForEdit(index) {
            editIndex = index
            let item = driveMappingsModel.get(index)
            driveLetterField.text = item.driveLetter
            hostPathField.text = item.hostPath
            descriptionField.text = item.description
            open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            GridLayout {
                columns: 2
                rowSpacing: 8
                columnSpacing: 16
                Layout.fillWidth: true

                Label { text: "Drive Letter:" }
                ComboBox {
                    id: driveLetterField
                    Layout.fillWidth: true
                    model: ["D:", "E:", "F:", "G:", "H:", "I:", "J:", "K:", "L:", "M:", "N:", "O:", "P:", "Q:", "R:", "S:", "T:", "U:", "V:", "W:", "X:", "Y:", "Z:"]
                    editable: true
                }

                Label { text: "Host Path:" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: hostPathField
                        Layout.fillWidth: true
                        placeholderText: "/path/to/directory"
                    }

                    Button {
                        text: "..."
                        onClicked: folderDialog.open()
                    }
                }

                Label { text: "Description:" }
                TextField {
                    id: descriptionField
                    Layout.fillWidth: true
                    placeholderText: "Optional description"
                }
            }

            Text {
                text: "Special paths:\n" +
                      "  ~ = Home directory\n" +
                      "  / = Root filesystem"
                font.pixelSize: 11
                color: palette.text
                opacity: 0.6
            }
        }

        FolderDialog {
            id: folderDialog
            title: "Select Host Directory"
            onAccepted: {
                hostPathField.text = selectedFolder.toString().replace("file://", "")
            }
        }

        onAccepted: {
            let driveLetter = driveLetterField.editText || driveLetterField.currentText
            if (editIndex >= 0) {
                driveMappingsModel.set(editIndex, {
                    driveLetter: driveLetter,
                    hostPath: hostPathField.text,
                    description: descriptionField.text,
                    enabled: true
                })
            } else {
                driveMappingsModel.append({
                    driveLetter: driveLetter,
                    hostPath: hostPathField.text,
                    description: descriptionField.text,
                    enabled: true
                })
            }
        }
    }

    onAccepted: {
        let mappings = []
        for (let i = 0; i < driveMappingsModel.count; i++) {
            mappings.push(driveMappingsModel.get(i))
        }
        mappingsApplied(mappings)
    }
}
