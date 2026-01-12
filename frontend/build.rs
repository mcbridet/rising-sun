use cxx_qt_build::{CxxQtBuilder, QmlModule};

fn main() {
    // Rebuild if Qt version preference changes
    println!("cargo::rerun-if-env-changed=QT_VERSION_MAJOR");
    
    CxxQtBuilder::new()
        .qml_module(QmlModule {
            uri: "com.risingsun",
            rust_files: &[
                "src/ui/main_window.rs",
                "src/ui/config_manager.rs",
                "src/ui/settings_controller.rs",
                "src/ui/disk_manager.rs",
                "src/ui/drive_mapping_controller.rs",
                "src/ui/session_controller.rs",
                "src/ui/display_view.rs",
                "src/ui/network_controller.rs",
                "src/ui/input_controller.rs",
                "src/ui/audio_controller.rs",
                "src/ui/clipboard_controller.rs",
            ],
            qml_files: &[
                "qml/main.qml",
                // Dialogs
                "qml/dialogs/CreateDiskDialog.qml",
                "qml/dialogs/DiskPropertiesDialog.qml",
                "qml/dialogs/DisplaySettingsDialog.qml",
                "qml/dialogs/KeyboardSettingsDialog.qml",
                "qml/dialogs/MouseSettingsDialog.qml",
                "qml/dialogs/DriveMappingDialog.qml",
                "qml/dialogs/ClipboardSettingsDialog.qml",
                "qml/dialogs/NetworkSettingsDialog.qml",
                "qml/dialogs/MountIsoDialog.qml",
                "qml/dialogs/MountFloppyDialog.qml",
            ],
            ..Default::default()
        })
        .build();
}
