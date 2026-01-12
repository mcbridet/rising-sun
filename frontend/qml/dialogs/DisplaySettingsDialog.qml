import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

// Dialog for display presentation settings
// Note: Resolution and color depth are controlled by the GUEST OS
// (via INT 10h or Windows display drivers like spcdisp.drv/sunvideo.dll)
// This dialog only controls how the host presents the guest output.
// Based on SunPCi display from analysis/03-display-emulation.md
Dialog {
    id: displaySettingsDialog
    title: "Display Settings"
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: 450
    height: Math.min(550, Screen.height - 100)

    // Reference to config manager
    required property var config

    signal settingsApplied()

    // Load current values when dialog opens
    onOpened: {
        aspectRatioCheck.checked = config.get_maintain_aspect_ratio()
        scanlineCheck.checked = config.get_scanline_effect()
        integerScaleRadio.checked = config.get_integer_scaling()
        if (!integerScaleRadio.checked) {
            fitWindowRadio.checked = true
        }
    }

    // Apply settings
    function applySettings() {
        config.set_maintain_aspect_ratio_value(aspectRatioCheck.checked)
        config.set_scanline_effect_value(scanlineCheck.checked)
        config.set_integer_scaling_value(integerScaleRadio.checked)
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

            // Info about guest-controlled resolution
            GroupBox {
                title: "Current Display"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    GridLayout {
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 16

                        Label { text: "Resolution:"; font.bold: true }
                        Label { 
                            id: currentResolution
                            text: "640 × 480"  // Updated dynamically from guest
                        }

                        Label { text: "Color Depth:"; font.bold: true }
                        Label { 
                            id: currentColorDepth
                            text: "8-bit (256 colors)"  // Updated dynamically from guest
                        }

                        Label { text: "Mode:"; font.bold: true }
                        Label { 
                            id: currentMode
                            text: "VGA Graphics"  // Text or Graphics
                        }
                    }

                    Text {
                        text: "Resolution and color depth are controlled by the guest OS.\n" +
                              "Change display settings in Windows Control Panel or via\n" +
                              "DOS MODE command."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Display scaling (host presentation)
            GroupBox {
                title: "Scaling"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RadioButton {
                        id: noScaleRadio
                        text: "No scaling (1:1 pixels)"
                    }

                    RadioButton {
                        id: fitWindowRadio
                        text: "Fit to window"
                    }

                    RadioButton {
                        id: integerScaleRadio
                        text: "Integer scaling (2×, 3×, etc.)"
                    }

                    RowLayout {
                        spacing: 16
                        Layout.leftMargin: 24
                        enabled: integerScaleRadio.checked

                        Label { text: "Scale factor:" }
                        SpinBox {
                            id: scaleFactorSpinBox
                            from: 1
                            to: 4
                            value: 2
                        }
                    }
                }
            }

            // Presentation options
            GroupBox {
                title: "Presentation"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: aspectRatioCheck
                        text: "Maintain aspect ratio (4:3)"
                    }

                    CheckBox {
                        id: smoothScalingCheck
                        text: "Smooth scaling (bilinear filter)"
                        enabled: !noScaleRadio.checked

                        Text {
                            anchors.left: parent.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "(may blur pixels)"
                            font.pixelSize: 10
                            color: palette.text
                            opacity: 0.5
                            visible: parent.enabled
                        }
                    }

                    CheckBox {
                        id: scanlineCheck
                        text: "CRT scanline effect"
                    }
                }
            }

            // Window options
            GroupBox {
                title: "Window"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox {
                        id: fullscreenCheck
                        text: "Start in fullscreen mode"
                    }

                    CheckBox {
                        id: hideMenuFullscreenCheck
                        text: "Hide menu bar in fullscreen"
                        enabled: fullscreenCheck.checked
                    }

                    CheckBox {
                        id: alwaysOnTopCheck
                        text: "Keep window on top"
                    }
                }
            }
        }
    }  // ScrollView

    onApplied: applySettings()
}
