import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

// Dialog for keyboard and input settings
// Based on SunPCi keyboard analysis from analysis/04-keyboard-mouse.md
Dialog {
    id: keyboardSettingsDialog
    title: "Keyboard Settings"
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: 450
    height: Math.min(550, Screen.height - 100)

    // Reference to config manager
    required property var config

    signal settingsApplied()

    // Load current values when dialog opens
    onOpened: {
        // Find index for current layout
        let layout = config.get_keyboard_layout()
        for (let i = 0; i < layoutCombo.model.count; i++) {
            if (layoutCombo.model.get(i).code === layout) {
                layoutCombo.currentIndex = i
                break
            }
        }
        
        // Find index for current code page
        let codePage = config.get_code_page()
        for (let i = 0; i < codePageCombo.model.count; i++) {
            if (codePageCombo.model.get(i).code === codePage) {
                codePageCombo.currentIndex = i
                break
            }
        }
    }

    // Apply settings
    function applySettings() {
        let layout = layoutCombo.model.get(layoutCombo.currentIndex).code
        let codePage = codePageCombo.model.get(codePageCombo.currentIndex).code
        config.set_keyboard_layout_value(layout)
        config.set_code_page_value(codePage)
        config.save()
        settingsApplied()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 16

            // Keyboard layout selection
            GroupBox {
                title: "Keyboard Layout"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    ComboBox {
                        id: layoutCombo
                        Layout.fillWidth: true
                        // Supported layouts from analysis/04-keyboard-mouse.md
                        model: ListModel {
                            ListElement { text: "US English (QWERTY)"; code: "us" }
                            ListElement { text: "UK English"; code: "uk" }
                            ListElement { text: "German (QWERTZ)"; code: "de" }
                            ListElement { text: "French (AZERTY)"; code: "fr" }
                            ListElement { text: "Spanish"; code: "sp" }
                            ListElement { text: "Italian"; code: "it" }
                            ListElement { text: "Portuguese"; code: "po" }
                            ListElement { text: "Dutch"; code: "nl" }
                            ListElement { text: "Belgian"; code: "be" }
                            ListElement { text: "Danish"; code: "dk" }
                            ListElement { text: "Norwegian"; code: "no" }
                            ListElement { text: "Swedish"; code: "sv" }
                            ListElement { text: "Finnish (Suomi)"; code: "su" }
                            ListElement { text: "Swiss French"; code: "sf" }
                            ListElement { text: "Swiss German"; code: "sg" }
                            ListElement { text: "Canadian French"; code: "cf" }
                            ListElement { text: "Latin American"; code: "la" }
                        }
                        textRole: "text"
                        currentIndex: 0
                    }

                    Text {
                        text: "Keyboard layout affects how keys are mapped to DOS scancodes."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Code page selection
            GroupBox {
                title: "DOS Code Page"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    ComboBox {
                        id: codePageCombo
                        Layout.fillWidth: true
                        // Supported code pages from analysis/04-keyboard-mouse.md
                        model: ListModel {
                            ListElement { text: "437 - US English (default)"; code: "437" }
                            ListElement { text: "850 - Multilingual Latin-1"; code: "850" }
                            ListElement { text: "860 - Portuguese"; code: "860" }
                            ListElement { text: "863 - Canadian French"; code: "863" }
                            ListElement { text: "865 - Nordic"; code: "865" }
                        }
                        textRole: "text"
                        currentIndex: 0
                    }

                    Text {
                        text: "Code page affects character encoding for non-ASCII characters.\n" +
                              "Used for Compose key sequences."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Keyboard capture options
            GroupBox {
                title: "Capture Options"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        spacing: 8
                        
                        Label { text: "Host key (release capture):" }
                        
                        ComboBox {
                            id: hostKeyCombo
                            model: ["Right Ctrl", "Right Alt", "Left Ctrl + Left Alt"]
                            currentIndex: 0
                        }
                    }

                    CheckBox {
                        id: autoCaptureCheck
                        text: "Auto-capture keyboard on window focus"
                    }

                    CheckBox {
                        id: passSpecialKeysCheck
                        text: "Pass special keys to guest (Ctrl+Alt+Del, etc.)"
                    }
                }
            }

            // Modifier state sync
            GroupBox {
                title: "Modifier Synchronization"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: syncCapsLockCheck
                        text: "Synchronize Caps Lock state"
                    }

                    CheckBox {
                        id: syncNumLockCheck
                        text: "Synchronize Num Lock state"
                    }

                    CheckBox {
                        id: syncScrollLockCheck
                        text: "Synchronize Scroll Lock state"
                    }
                }
            }
        }
    }  // ScrollView

    onApplied: applySettings()
}
