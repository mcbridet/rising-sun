import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Dialog for clipboard/copy-paste settings
// Based on SunPCI clipboard analysis from analysis/06-clipboard.md
Dialog {
    id: clipboardSettingsDialog
    title: "Clipboard Settings"
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: 400
    height: Math.min(480, Screen.height - 100)

    // Reference to config manager
    required property var config

    signal settingsApplied(bool enabled, string direction)

    // Load current values when dialog opens
    onOpened: {
        enableClipboardCheck.checked = config.get_clipboard_enabled()
    }

    // Get current direction as string
    function getDirection() {
        if (bidirectionalRadio.checked) return "bidirectional"
        if (hostToGuestRadio.checked) return "host_to_guest"
        if (guestToHostRadio.checked) return "guest_to_host"
        return "bidirectional"
    }

    // Apply settings
    function applySettings() {
        config.set_clipboard_enabled_value(enableClipboardCheck.checked)
        config.save()
        settingsApplied(enableClipboardCheck.checked, getDirection())
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 16

            // Info
            Text {
                Layout.fillWidth: true
                text: "Configure clipboard sharing between host and guest.\n" +
                      "Requires sunclip.exe running in guest."
                font.pixelSize: 12
                color: palette.text
                wrapMode: Text.WordWrap
            }

            // Enable/disable
            GroupBox {
                title: "Clipboard Integration"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    CheckBox {
                        id: enableClipboardCheck
                        text: "Enable clipboard sharing"
                    }

                    Text {
                        text: "When enabled, copy/paste works between host and guest."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                        visible: enableClipboardCheck.checked
                    }
                }
            }

            // Direction
            GroupBox {
                title: "Transfer Direction"
                Layout.fillWidth: true
                enabled: enableClipboardCheck.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RadioButton {
                        id: bidirectionalRadio
                        text: "Bidirectional"
                        checked: true
                    }

                    RadioButton {
                        id: hostToGuestRadio
                        text: "Host to Guest only"
                    }

                    RadioButton {
                        id: guestToHostRadio
                        text: "Guest to Host only"
                    }
                }
            }

            // Supported formats
            GroupBox {
                title: "Data Types"
                Layout.fillWidth: true
                enabled: enableClipboardCheck.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: textFormatCheck
                        text: "Plain text (CF_TEXT)"
                        checked: true
                    }

                    CheckBox {
                        id: unicodeFormatCheck
                        text: "Unicode text (CF_UNICODETEXT)"
                        checked: true
                    }

                    CheckBox {
                        id: bitmapFormatCheck
                        text: "Bitmap images (CF_BITMAP)"
                        checked: false
                        enabled: false  // Not yet implemented
                        
                        Text {
                            anchors.left: parent.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "(not yet supported)"
                            font.pixelSize: 10
                            color: palette.text
                            opacity: 0.5
                        }
                    }
                }
            }

            // Size limits
            GroupBox {
                title: "Limits"
                Layout.fillWidth: true
                enabled: enableClipboardCheck.checked

                RowLayout {
                    anchors.fill: parent
                    spacing: 16

                    Label { text: "Maximum size:" }

                    SpinBox {
                        id: maxSizeSpinBox
                        from: 64
                        to: 16384
                        value: 1024
                        stepSize: 64
                        
                        textFromValue: function(value) {
                            return value + " KB"
                        }
                        valueFromText: function(text) {
                            return parseInt(text)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }  // ScrollView

    onApplied: applySettings()
}
