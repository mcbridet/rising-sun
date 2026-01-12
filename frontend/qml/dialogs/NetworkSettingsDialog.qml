import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

// Dialog for network adapter settings
// Based on SunPCi NDIS analysis from analysis/07-network.md
Dialog {
    id: networkSettingsDialog
    title: "Network Settings"
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: 480
    height: Math.min(550, Screen.height - 100)

    // Reference to config manager
    required property var config

    signal settingsApplied()

    // Load current values when dialog opens
    onOpened: {
        enableNetworkCheck.checked = config.get_network_enabled()
        macAddressField.text = config.get_mac_address()
    }

    // Apply settings
    function applySettings() {
        config.set_network_enabled_value(enableNetworkCheck.checked)
        if (customMacRadio.checked) {
            config.set_mac_address_value(macAddressField.text)
        }
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

            // Network adapter enable
            GroupBox {
                title: "Network Adapter"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    CheckBox {
                        id: enableNetworkCheck
                        text: "Enable network adapter"
                    }

                    Text {
                        text: "The guest will see an Ethernet adapter that bridges to the host network."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Host interface selection
            GroupBox {
                title: "Host Network Interface"
                Layout.fillWidth: true
                enabled: enableNetworkCheck.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    ComboBox {
                        id: interfaceCombo
                        Layout.fillWidth: true
                        // This would be populated dynamically from system interfaces
                        model: ListModel {
                            ListElement { text: "Auto-detect"; value: "auto" }
                            ListElement { text: "eth0 - Ethernet"; value: "eth0" }
                            ListElement { text: "enp0s3 - Ethernet"; value: "enp0s3" }
                            ListElement { text: "wlan0 - Wireless"; value: "wlan0" }
                            ListElement { text: "br0 - Bridge"; value: "br0" }
                        }
                        textRole: "text"
                        currentIndex: 0
                    }

                    Text {
                        text: "Select the host network interface to bridge.\n" +
                              "Environment variable: NVL_INTERFACE"
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // MAC address
            GroupBox {
                title: "MAC Address"
                Layout.fillWidth: true
                enabled: enableNetworkCheck.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        spacing: 16

                        RadioButton {
                            id: autoMacRadio
                            text: "Auto-generate"
                            checked: macAddressField.text === ""
                        }

                        RadioButton {
                            id: customMacRadio
                            text: "Custom:"
                            checked: macAddressField.text !== ""
                        }

                        TextField {
                            id: macAddressField
                            enabled: customMacRadio.checked
                            inputMask: "HH:HH:HH:HH:HH:HH;_"
                            font.family: "monospace"
                            Layout.preferredWidth: 140
                        }
                    }

                    Text {
                        text: "MAC address identifies the guest on the network.\n" +
                              "Auto-generated addresses use the 00:00:00 prefix."
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.6
                    }
                }
            }

            // Guest driver info
            GroupBox {
                title: "Guest Driver Information"
                Layout.fillWidth: true
                enabled: enableNetworkCheck.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Text {
                        text: "Guest drivers required:"
                        font.bold: true
                        color: palette.text
                    }

                    Text {
                        text: "• DOS: Packet driver (sunpcnet.exe)\n" +
                              "• Windows 95/98: sunwndis.vxd (NDIS 3.x)\n" +
                              "• Windows NT/2000: sunndis.sys (NDIS 5.x)"
                        font.pixelSize: 11
                        color: palette.text
                        opacity: 0.8
                    }
                }
            }

            // IRQ selection
            GroupBox {
                title: "Advanced"
                Layout.fillWidth: true
                enabled: enableNetworkCheck.checked

                RowLayout {
                    anchors.fill: parent
                    spacing: 16

                    Label { text: "IRQ:" }

                    ComboBox {
                        id: irqCombo
                        model: ["Auto", "IRQ 9", "IRQ 10", "IRQ 11", "IRQ 15"]
                        currentIndex: 0
                    }

                    Item { Layout.fillWidth: true }

                    CheckBox {
                        id: promiscuousCheck
                        text: "Promiscuous mode"
                    }
                }
            }
        }
    }  // ScrollView

    onApplied: applySettings()
}
