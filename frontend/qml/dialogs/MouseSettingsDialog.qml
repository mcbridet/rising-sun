import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Dialog for mouse/pointer settings
// Based on SunPCi mouse analysis from analysis/04-keyboard-mouse.md
Dialog {
    id: mouseSettingsDialog
    title: "Mouse Settings"
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: 400
    height: Math.min(500, Screen.height - 100)

    // Reference to config manager
    required property var config

    signal settingsApplied()

    // Apply settings
    function applySettings() {
        // TODO: Add mouse settings to ConfigManager when needed
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

            // Mouse protocol
            GroupBox {
                title: "Mouse Protocol"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    ComboBox {
                        id: protocolCombo
                        Layout.fillWidth: true
                        model: ListModel {
                            ListElement { text: "PS/2 Mouse (recommended)"; value: "ps2" }
                            ListElement { text: "Serial Mouse (COM1)"; value: "serial" }
                            ListElement { text: "Absolute Positioning (tablet mode)"; value: "absolute" }
                        }
                        textRole: "text"
                        currentIndex: 0
                    }

                    Text {
                        text: "PS/2 mouse is recommended for Windows 95 and later.\n" +
                              "Serial mouse may be needed for older DOS applications."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Capture behavior
            GroupBox {
                title: "Capture Behavior"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RadioButton {
                        id: clickCaptureRadio
                        text: "Capture on click"
                    }

                    RadioButton {
                        id: autoCaptureRadio
                        text: "Auto-capture when guest requests"
                    }

                    RadioButton {
                        id: neverCaptureRadio
                        text: "Never capture (use absolute positioning)"
                    }

                    Text {
                        text: "When captured, press the Host Key to release."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Pointer integration
            GroupBox {
                title: "Pointer Integration"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: seamlessCheck
                        text: "Seamless mouse integration"
                        ToolTip.text: "Requires guest driver support"
                        ToolTip.visible: hovered
                    }

                    CheckBox {
                        id: hideCursorCheck
                        text: "Hide host cursor when captured"
                    }

                    CheckBox {
                        id: accelerationCheck
                        text: "Use host mouse acceleration"
                    }
                }
            }

            // Button mapping
            GroupBox {
                title: "Button Mapping"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: swapButtonsCheck
                        text: "Swap left and right buttons"
                    }

                    CheckBox {
                        id: middleButtonCheck
                        text: "Simulate middle button (left + right click)"
                    }
                }
            }
        }
    }  // ScrollView

    onApplied: applySettings()
}
