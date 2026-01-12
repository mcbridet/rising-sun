import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.risingsun
import "dialogs"

ApplicationWindow {
    id: window
    visible: true
    width: 800
    height: 600
    minimumWidth: 640
    minimumHeight: 480
    title: "Rising Sun"

    MainWindow {
        id: mainWindow
    }

    // Session controller for driver communication
    SessionController {
        id: sessionController
        Component.onCompleted: check_driver()
    }

    // Disk manager for disk image operations
    DiskManager {
        id: diskManager
    }

    // Configuration manager for persistent settings
    ConfigManager {
        id: configManager
        Component.onCompleted: load()
    }

    // Input controller for keyboard and mouse handling
    InputController {
        id: inputController
        guest_width: sessionController.display_width
        guest_height: sessionController.display_height
        
        // Connect to driver when session starts
        Component.onCompleted: {
            if (sessionController.session_running) {
                driver_fd = sessionController.get_driver_fd()
            }
        }
        
        onKeyboard_capturedChanged: {
            console.log("Keyboard capture:", keyboard_captured)
        }
        
        onMouse_capturedChanged: {
            console.log("Mouse capture:", mouse_captured)
            if (mouse_captured) {
                displayOutput.cursorShape = Qt.BlankCursor
            } else {
                displayOutput.cursorShape = Qt.ArrowCursor
            }
        }
    }
    
    // Update input controller when session state changes
    Connections {
        target: sessionController
        function onSession_runningChanged() {
            if (sessionController.session_running) {
                inputController.driver_fd = sessionController.get_driver_fd()
                audioController.init_audio(sessionController.get_driver_fd())
                if (audioController.audio_available && audioController.audio_enabled) {
                    audioController.start_playback()
                }
                clipboardController.init_clipboard(sessionController.get_driver_fd())
                networkController.init_network(sessionController.get_driver_fd())
                if (networkController.network_enabled) {
                    networkController.apply_config()
                }
                driveMappingController.init_mappings(sessionController.get_driver_fd())
                driveMappingController.apply_mappings()
            } else {
                inputController.release_capture()
                audioController.stop_playback()
            }
        }
    }

    // Audio controller for sound output
    AudioController {
        id: audioController
        audio_enabled: true
        
        Component.onCompleted: {
            if (sessionController.session_running) {
                init_audio(sessionController.get_driver_fd())
            }
        }
    }
    
    // Audio status polling (less frequent than display)
    Timer {
        id: audioStatusTimer
        interval: 250  // 4 Hz
        repeat: true
        running: sessionController.session_running && audioController.audio_available
        onTriggered: audioController.poll_status()
    }

    // Clipboard controller for host/guest clipboard sync
    ClipboardController {
        id: clipboardController
        clipboard_enabled: configManager.get_clipboard_enabled()
        host_to_guest: true
        guest_to_host: true
        
        Component.onCompleted: {
            if (sessionController.session_running) {
                init_clipboard(sessionController.get_driver_fd())
            }
        }
        
        // When guest clipboard changes, update host clipboard
        onGuest_clipboard_changed: (text) => {
            // Use Qt.callLater to avoid reentrancy
            Qt.callLater(function() {
                if (text.length > 0) {
                    // Note: In Qt6 QML, we need to use Clipboard API
                    // This signal is emitted when guest has new content
                    console.log("Guest clipboard:", text.substring(0, 50) + (text.length > 50 ? "..." : ""))
                }
            })
        }
    }
    
    // Clipboard polling timer (check guest clipboard periodically)
    Timer {
        id: clipboardPollTimer
        interval: 500  // 2 Hz - check for guest clipboard changes
        repeat: true
        running: sessionController.session_running && clipboardController.clipboard_enabled && clipboardController.guest_to_host
        onTriggered: clipboardController.poll_guest_clipboard()
    }

    // Network controller for virtual NIC management
    NetworkController {
        id: networkController
        network_enabled: configManager.get_network_enabled()
        
        Component.onCompleted: {
            if (sessionController.session_running) {
                init_network(sessionController.get_driver_fd())
            }
        }
        
        onStatus_changed: {
            console.log("Network status changed:", status_text)
        }
        
        onConfig_error: (message) => {
            console.error("Network error:", message)
        }
    }

    // Drive mapping controller for host filesystem redirection
    DriveMappingController {
        id: driveMappingController
        
        Component.onCompleted: {
            // Load default mappings
            load_mappings_json(get_default_mappings_json())
            
            if (sessionController.session_running) {
                init_mappings(sessionController.get_driver_fd())
            }
        }
    }
    
    // Network status polling (slow - just for stats)
    Timer {
        id: networkStatusTimer
        interval: 1000  // 1 Hz
        repeat: true
        running: sessionController.session_running && networkController.network_enabled
        onTriggered: networkController.poll_status()
    }

    // Display refresh timer (60 FPS when running)
    Timer {
        id: displayRefreshTimer
        interval: 16  // ~60 FPS
        repeat: true
        running: sessionController.session_running
        onTriggered: {
            sessionController.poll_display()
            displayImage.source = ""  // Force reload
            displayImage.source = "image://framebuffer/frame?" + Date.now()
        }
    }

    // Save config when window closes
    onClosing: (close) => {
        configManager.save()
    }

    // Global keyboard shortcuts
    Shortcut {
        sequence: StandardKey.Quit
        onActivated: quitAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+R"
        enabled: !sessionController.session_running && !sessionController.session_starting
        onActivated: startAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: sessionController.session_running
        onActivated: resetAction.trigger()
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action {
                text: qsTr("&About")
                onTriggered: aboutDialog.open()
            }
            MenuSeparator {}
            Action {
                id: quitAction
                text: qsTr("&Quit") + "\t" + "Ctrl+Q"
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: qsTr("&Machine")
            Action {
                id: startAction
                text: qsTr("&Start") + "\t" + "Ctrl+R"
                enabled: !sessionController.session_running && !sessionController.session_starting && sessionController.driver_loaded
                onTriggered: {
                    sessionController.start_session()
                }
            }
            Action {
                id: resetAction
                text: qsTr("&Reset") + "\t" + "Ctrl+Shift+R"
                enabled: sessionController.session_running
                onTriggered: {
                    sessionController.reset_session()
                }
            }
            MenuSeparator {}
            Action {
                text: qsTr("S&top")
                enabled: sessionController.session_running
                onTriggered: {
                    sessionController.stop_session()
                }
            }
        }

        Menu {
            title: qsTr("&Devices")
            Menu {
                title: qsTr("&Hard Disks")
                Action {
                    text: qsTr("C: Primary...")
                    onTriggered: diskPropertiesDialog.open()
                }
                Action {
                    text: qsTr("D: Secondary...")
                }
                MenuSeparator {}
                Action {
                    text: qsTr("&Create Disk Image...")
                    onTriggered: createDiskDialog.open()
                }
            }
            Menu {
                title: qsTr("&Floppy Drives")
                Action {
                    text: qsTr("A: Mount Image...")
                    onTriggered: {
                        mountFloppyDialog.driveNumber = 0
                        mountFloppyDialog.open()
                    }
                }
                Action {
                    text: qsTr("A: Eject")
                    enabled: diskManager.floppy_a_mounted
                    onTriggered: diskManager.eject_floppy(0)
                }
                MenuSeparator {}
                Action {
                    text: qsTr("B: Mount Image...")
                    onTriggered: {
                        mountFloppyDialog.driveNumber = 1
                        mountFloppyDialog.open()
                    }
                }
                Action {
                    text: qsTr("B: Eject")
                    enabled: diskManager.floppy_b_mounted
                    onTriggered: diskManager.eject_floppy(1)
                }
                MenuSeparator {}
                Action {
                    text: qsTr("&Eject All")
                    enabled: diskManager.floppy_a_mounted || diskManager.floppy_b_mounted
                    onTriggered: {
                        diskManager.eject_floppy(0)
                        diskManager.eject_floppy(1)
                    }
                }
            }
            Menu {
                title: qsTr("&CD-ROM")
                Action {
                    text: qsTr("&Mount ISO Image...")
                    onTriggered: mountIsoDialog.open()
                }
                Action {
                    text: qsTr("&Eject")
                    enabled: diskManager.cdrom_mounted
                    onTriggered: {
                        diskManager.eject_cdrom()
                        mountIsoDialog.isMounted = false
                        mountIsoDialog.selectedIsoPath = ""
                    }
                }
            }
            MenuSeparator {}
            Menu {
                title: qsTr("&Network")
                Action {
                    text: qsTr("&Settings...")
                    onTriggered: networkSettingsDialog.open()
                }
                MenuSeparator {}
                Action {
                    text: qsTr("&Enable Adapter")
                    checkable: true
                    checked: true
                }
            }
            Menu {
                title: qsTr("&Shared Folders")
                Action {
                    text: qsTr("&Drive Mappings...")
                    onTriggered: driveMappingDialog.open()
                }
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Clipboard Settings...")
                onTriggered: clipboardSettingsDialog.open()
            }
        }

        Menu {
            title: qsTr("&View")
            Action {
                text: qsTr("&Fullscreen")
                shortcut: "F11"
                checkable: true
                onTriggered: {
                    if (checked) {
                        window.showFullScreen()
                    } else {
                        window.showNormal()
                    }
                }
            }
            MenuSeparator {}
            Menu {
                title: qsTr("&Scaling")
                
                // Scaling mode: 0 = None (1:1), 1 = Fit to Window, 2 = Integer Scaling
                property int scalingMode: {
                    if (configManager.get_integer_scaling()) return 2
                    if (configManager.get_maintain_aspect_ratio()) return 1
                    return 0
                }
                
                Action {
                    text: qsTr("&None (1:1)")
                    checkable: true
                    checked: parent.scalingMode === 0
                    onTriggered: {
                        configManager.set_integer_scaling_value(false)
                        configManager.set_maintain_aspect_ratio_value(false)
                    }
                }
                Action {
                    text: qsTr("&Fit to Window")
                    checkable: true
                    checked: parent.scalingMode === 1
                    onTriggered: {
                        configManager.set_integer_scaling_value(false)
                        configManager.set_maintain_aspect_ratio_value(true)
                    }
                }
                Action {
                    text: qsTr("&Integer Scaling")
                    checkable: true
                    checked: parent.scalingMode === 2
                    onTriggered: {
                        configManager.set_integer_scaling_value(true)
                        configManager.set_maintain_aspect_ratio_value(false)
                    }
                }
            }
            Action {
                text: qsTr("&Display Settings...")
                onTriggered: displaySettingsDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Status Bar")
                checkable: true
                checked: true
                onTriggered: statusBar.visible = checked
            }
        }

        Menu {
            title: qsTr("&Input")
            Action {
                text: qsTr("&Keyboard Capture")
                checkable: true
                checked: inputController.keyboard_captured
                shortcut: "Right Ctrl"
                onTriggered: inputController.toggle_keyboard_capture()
            }
            Action {
                text: qsTr("&Mouse Capture")
                checkable: true
                checked: inputController.mouse_captured
                onTriggered: inputController.toggle_mouse_capture()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Send Ctrl+Alt+&Del")
                enabled: sessionController.session_running
                onTriggered: {
                    inputController.send_ctrl_alt_del()
                }
            }
            Action {
                text: qsTr("Send Ctrl+Alt+&Backspace")
                enabled: sessionController.session_running
                onTriggered: {
                    inputController.send_ctrl_alt_backspace()
                }
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Keyboard Settings...")
                onTriggered: keyboardSettingsDialog.open()
            }
            Action {
                text: qsTr("&Mouse Settings...")
                onTriggered: mouseSettingsDialog.open()
            }
        }
    }

    // About dialog
    Dialog {
        id: aboutDialog
        title: "Rising Sun"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok

        ColumnLayout {
            spacing: 16

            Text {
                text: "Rising Sun"
                font.pixelSize: 18
                font.bold: true
                color: palette.text
            }

            Text {
                text: "Frontend Version: 0.1.0"
                font.pixelSize: 12
                color: palette.text
            }

            Text {
                text: "Driver Version: " + sessionController.driver_version
                font.pixelSize: 12
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }

            Text {
                text: "A modern reimplementation of SunPCI\nfor Linux with the original hardware."
                font.pixelSize: 12
                color: palette.text
                opacity: 0.8
            }
        }
    }

    // Main content area
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Display output - black when no output
        Rectangle {
            id: displayOutput
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            // Framebuffer display image
            Image {
                id: displayImage
                anchors.centerIn: parent
                width: sessionController.display_width * displayScale
                height: sessionController.display_height * displayScale
                source: sessionController.session_running ? "image://framebuffer/frame" : ""
                cache: false
                smooth: !configManager.get_integer_scaling()
                visible: sessionController.session_running

                // Calculate scale based on settings
                property real displayScale: {
                    if (configManager.get_integer_scaling()) {
                        // Integer scaling - find largest integer that fits
                        var xScale = Math.floor(displayOutput.width / sessionController.display_width)
                        var yScale = Math.floor(displayOutput.height / sessionController.display_height)
                        return Math.max(1, Math.min(xScale, yScale))
                    } else if (configManager.get_maintain_aspect_ratio()) {
                        // Fit to window maintaining aspect ratio
                        var xRatio = displayOutput.width / sessionController.display_width
                        var yRatio = displayOutput.height / sessionController.display_height
                        return Math.min(xRatio, yRatio)
                    } else {
                        // Stretch to fill (handled by anchors.fill instead)
                        return 1.0
                    }
                }
            }

            // Placeholder text shown when session not running
            Column {
                anchors.centerIn: parent
                spacing: 16
                visible: !sessionController.session_running

                // Driver status icon/indicator
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 80
                    height: 80
                    radius: 40
                    color: sessionController.driver_loaded ? "#2a4a2a" : "#4a2a2a"
                    border.color: sessionController.driver_loaded ? "#4a8a4a" : "#8a4a4a"
                    border.width: 2

                    Text {
                        anchors.centerIn: parent
                        text: sessionController.driver_loaded ? "✓" : "✗"
                        font.pixelSize: 40
                        color: sessionController.driver_loaded ? "#6aba6a" : "#ba6a6a"
                    }
                }

                // Main status message
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (sessionController.session_starting) {
                            return "Starting session..."
                        } else if (!sessionController.driver_loaded) {
                            return "SunPCI Driver Not Loaded"
                        } else {
                            return "Ready to Start"
                        }
                    }
                    color: sessionController.driver_loaded ? "#888888" : "#aa6666"
                    font.pixelSize: 18
                    font.bold: true
                }

                // Detailed status / instructions
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (!sessionController.driver_loaded) {
                            return "The sunpci kernel module is not loaded.\nRun: sudo insmod driver/sunpci.ko\nor: sudo modprobe sunpci"
                        } else {
                            return "Press Ctrl+R or use Machine → Start"
                        }
                    }
                    color: "#666666"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.4
                }

                // Retry button when driver not loaded
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Check Again"
                    visible: !sessionController.driver_loaded
                    onClicked: sessionController.check_driver()
                }
            }

            // Error display
            Text {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 30
                text: sessionController.error_message
                color: "#aa3333"
                font.pixelSize: 12
                visible: sessionController.session_error
            }

            // Focus handling for keyboard input
            focus: true
            Keys.onPressed: (event) => {
                if (sessionController.session_running) {
                    var handled = inputController.handle_key_press(
                        event.key,
                        event.modifiers,
                        event.nativeScanCode
                    )
                    event.accepted = handled || inputController.keyboard_captured
                }
            }
            Keys.onReleased: (event) => {
                if (sessionController.session_running) {
                    var handled = inputController.handle_key_release(
                        event.key,
                        event.modifiers,
                        event.nativeScanCode
                    )
                    event.accepted = handled || inputController.keyboard_captured
                }
            }

            // Mouse handling - only active when session is running
            MouseArea {
                id: displayMouseArea
                anchors.fill: parent
                enabled: sessionController.session_running
                visible: sessionController.session_running
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                cursorShape: inputController.mouse_captured ? Qt.BlankCursor : Qt.ArrowCursor
                
                property real lastX: 0
                property real lastY: 0

                onPressed: (mouse) => {
                    parent.forceActiveFocus()
                    
                    // If not captured, first click captures
                    if (!inputController.mouse_captured) {
                        inputController.toggle_mouse_capture()
                        inputController.toggle_keyboard_capture()
                        lastX = mouse.x
                        lastY = mouse.y
                        return
                    }
                    
                    // Forward button press
                    var button = 0
                    if (mouse.button === Qt.LeftButton) button = 1
                    else if (mouse.button === Qt.RightButton) button = 2
                    else if (mouse.button === Qt.MiddleButton) button = 4
                    inputController.handle_mouse_press(button)
                }
                onReleased: (mouse) => {
                    if (!inputController.mouse_captured) return
                    
                    var button = 0
                    if (mouse.button === Qt.LeftButton) button = 1
                    else if (mouse.button === Qt.RightButton) button = 2
                    else if (mouse.button === Qt.MiddleButton) button = 4
                    inputController.handle_mouse_release(button)
                }
                onPositionChanged: (mouse) => {
                    if (!inputController.mouse_captured) {
                        lastX = mouse.x
                        lastY = mouse.y
                        return
                    }
                    
                    // Calculate relative movement
                    var dx = mouse.x - lastX
                    var dy = mouse.y - lastY
                    lastX = mouse.x
                    lastY = mouse.y
                    
                    if (dx !== 0 || dy !== 0) {
                        inputController.handle_mouse_move(dx, dy)
                    }
                }
                onWheel: (wheel) => {
                    if (!inputController.mouse_captured) return
                    inputController.handle_mouse_wheel(wheel.angleDelta.y)
                }
            }
        }

        // Status bar (VirtualBox-style)
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: "#2d2d2d"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 16

                // Hard disk indicator
                StatusIndicator {
                    icon: "HDD"
                    tooltipText: "Hard Disk"
                    active: sessionController.session_running
                }

                // Floppy indicator
                StatusIndicator {
                    icon: "FDD"
                    tooltipText: "Floppy Drive"
                    active: false
                }

                // Keyboard capture indicator
                StatusIndicator {
                    icon: "KBD"
                    tooltipText: inputController.keyboard_captured 
                        ? "Keyboard Capture: On\nPress Right Ctrl to release"
                        : "Keyboard Capture: Off\nClick in display to capture"
                    active: inputController.keyboard_captured
                }

                // Mouse capture indicator
                StatusIndicator {
                    icon: "MSE"
                    tooltipText: inputController.mouse_captured
                        ? "Mouse Capture: On\nPress Right Ctrl to release"
                        : "Mouse Capture: Off\nClick in display to capture"
                    active: inputController.mouse_captured
                }

                // Audio indicator
                StatusIndicator {
                    icon: audioController.audio_muted ? "🔇" : "🔊"
                    tooltipText: {
                        if (!audioController.audio_available) {
                            return "Audio: Not available"
                        } else if (audioController.audio_muted) {
                            return "Audio: Muted\nClick to unmute"
                        } else {
                            return "Audio: " + audioController.get_volume_percent() + "%\n" +
                                   audioController.sample_rate + " Hz, " +
                                   audioController.channels + " ch"
                        }
                    }
                    active: audioController.audio_playing && !audioController.audio_muted
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: audioController.toggle_mute()
                    }
                }

                // Spacer
                Item { Layout.fillWidth: true }

                // Resolution display
                Text {
                    text: sessionController.display_width + "×" + sessionController.display_height
                    color: "#888888"
                    font.pixelSize: 11
                    font.family: "monospace"
                }

                // Separator
                Rectangle {
                    width: 1
                    Layout.fillHeight: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    color: "#444444"
                }

                // Status text
                Text {
                    text: {
                        if (sessionController.session_starting) {
                            return "Starting..."
                        } else if (sessionController.session_running) {
                            return "Running"
                        } else {
                            return "Stopped"
                        }
                    }
                    color: sessionController.session_running ? "#88cc88" : "#888888"
                    font.pixelSize: 11
                }
            }
        }
    }

    // Status indicator component
    component StatusIndicator: Rectangle {
        id: indicator
        property string icon: ""
        property string tooltipText: ""
        property bool active: false

        implicitWidth: label.implicitWidth + 8
        height: 20
        color: mouseArea.containsMouse ? "#3d3d3d" : "transparent"
        radius: 2

        Text {
            id: label
            anchors.centerIn: parent
            text: indicator.icon
            font.pixelSize: 10
            font.family: "monospace"
            font.bold: true
            color: indicator.active ? "#88cc88" : "#888888"
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true

            onContainsMouseChanged: {
                if (containsMouse && indicator.tooltipText !== "") {
                    tooltipTimer.start()
                } else {
                    tooltipTimer.stop()
                    customTooltip.visible = false
                }
            }
        }

        Timer {
            id: tooltipTimer
            interval: 500
            onTriggered: customTooltip.visible = true
        }

        // Custom tooltip to avoid Breeze binding loop
        Rectangle {
            id: customTooltip
            visible: false
            x: indicator.width / 2 - width / 2
            y: -height - 4
            width: tooltipLabel.implicitWidth + 12
            height: tooltipLabel.implicitHeight + 8
            color: "#2d2d2d"
            border.color: "#555555"
            border.width: 1
            radius: 3
            z: 1000

            Text {
                id: tooltipLabel
                anchors.centerIn: parent
                text: indicator.tooltipText
                color: "#cccccc"
                font.pixelSize: 11
            }
        }
    }

    // ==========================================================================
    // Dialog instances
    // ==========================================================================

    // Create Disk Dialog - for creating new virtual disk images
    CreateDiskDialog {
        id: createDiskDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        onDiskCreated: (path, sizeMb, revision) => {
            console.log("Creating disk:", path, sizeMb, "MB, revision", revision)
            if (diskManager.create_disk(path, sizeMb, revision)) {
                console.log("Disk created successfully!")
            } else {
                console.log("Failed to create disk")
            }
        }
    }

    // Disk Properties Dialog - for viewing/editing disk settings
    DiskPropertiesDialog {
        id: diskPropertiesDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        // Properties will be populated when opening for a specific disk
        diskPath: ""
        diskSizeMb: 0
        revision: 2
        cylinders: 0
        heads: 0
        sectorsPerTrack: 0
        isBootable: false
    }

    // Display Settings Dialog
    // Note: Resolution/color depth are controlled by guest OS, not here
    DisplaySettingsDialog {
        id: displaySettingsDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        config: configManager

        onSettingsApplied: {
            console.log("Display presentation settings applied")
            // TODO: Apply scaling/fullscreen settings to renderer
        }
    }

    // Keyboard Settings Dialog
    KeyboardSettingsDialog {
        id: keyboardSettingsDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        config: configManager

        onSettingsApplied: {
            console.log("Keyboard settings applied")
            // TODO: Apply keyboard settings
        }
    }

    // Mouse Settings Dialog
    MouseSettingsDialog {
        id: mouseSettingsDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        config: configManager

        onSettingsApplied: {
            console.log("Mouse settings applied")
            // TODO: Apply mouse settings
        }
    }

    // Drive Mapping Dialog - for host filesystem redirection
    DriveMappingDialog {
        id: driveMappingDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        onMappingsApplied: (mappings) => {
            console.log("Drive mappings:", JSON.stringify(mappings))
            // Load the mappings into the controller
            driveMappingController.load_mappings_json(JSON.stringify(mappings))
            // Apply if session is running
            if (sessionController.session_running) {
                driveMappingController.apply_mappings()
            }
        }
    }

    // Clipboard Settings Dialog
    ClipboardSettingsDialog {
        id: clipboardSettingsDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        config: configManager

        onSettingsApplied: (enabled, direction) => {
            console.log("Clipboard settings applied: enabled=" + enabled + ", direction=" + direction)
            clipboardController.set_enabled(enabled)
            clipboardController.set_direction(direction)
        }
    }

    // Network Settings Dialog
    NetworkSettingsDialog {
        id: networkSettingsDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        config: configManager

        onSettingsApplied: {
            console.log("Network settings applied")
            networkController.set_enabled(configManager.get_network_enabled())
            // Interface would be read from the dialog's combo box
            // MAC would be read from the dialog's text field
            if (sessionController.session_running && networkController.network_enabled) {
                networkController.apply_config()
            }
        }
    }

    // Mount ISO Dialog - for CD-ROM support
    MountIsoDialog {
        id: mountIsoDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        onIsoMounted: (path) => {
            console.log("ISO mounted:", path)
            if (!diskManager.mount_iso(path)) {
                console.log("Failed to mount ISO")
            }
        }

        onIsoEjected: {
            console.log("ISO ejected")
            diskManager.eject_cdrom()
        }
    }

    // Mount Floppy Dialog - for floppy disk support
    MountFloppyDialog {
        id: mountFloppyDialog
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        onFloppyMounted: (path, drive) => {
            console.log("Floppy mounted:", path, "on drive", drive)
            if (!diskManager.mount_floppy(path, drive)) {
                console.log("Failed to mount floppy")
            }
        }

        onFloppyEjected: (drive) => {
            console.log("Floppy ejected from drive", drive)
            diskManager.eject_floppy(drive)
        }
    }
}
