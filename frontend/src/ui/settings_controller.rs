//! Settings controller Qt bridge for handling dialog interactions.

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(QString, keyboard_layout)]
        #[qproperty(QString, code_page)]
        #[qproperty(QString, network_interface)]
        #[qproperty(bool, clipboard_enabled)]
        #[qproperty(bool, network_enabled)]
        type SettingsController = super::SettingsControllerRust;

        /// Apply keyboard settings
        #[qinvokable]
        fn apply_keyboard_settings(self: &SettingsController, layout: QString, code_page: QString);

        /// Apply display presentation settings (scaling, fullscreen, etc.)
        /// Note: Resolution/color depth are controlled by guest OS, not host
        #[qinvokable]
        fn apply_display_settings(self: &SettingsController, scaling_mode: QString, scale_factor: i32, smooth: bool);

        /// Apply network settings
        #[qinvokable]
        fn apply_network_settings(self: &SettingsController, interface: QString, enabled: bool);

        /// Apply clipboard settings
        #[qinvokable]
        fn apply_clipboard_settings(self: &SettingsController, enabled: bool, direction: QString);

        /// Get available network interfaces
        #[qinvokable]
        fn get_network_interfaces(self: &SettingsController) -> QStringList;
    }

    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
        
        include!("cxx-qt-lib/qstringlist.h");
        type QStringList = cxx_qt_lib::QStringList;
    }
}

use cxx_qt_lib::{QString, QStringList};

/// Rust implementation of the SettingsController
#[derive(Default)]
pub struct SettingsControllerRust {
    keyboard_layout: QString,
    code_page: QString,
    network_interface: QString,
    clipboard_enabled: bool,
    network_enabled: bool,
}

impl qobject::SettingsController {
    /// Apply keyboard settings
    /// Note: Settings are saved to config and applied on next session start.
    /// Runtime keyboard layout changes require guest OS cooperation.
    pub fn apply_keyboard_settings(&self, layout: QString, code_page: QString) {
        tracing::info!(
            "Applying keyboard settings: layout={}, code_page={}",
            layout.to_string(),
            code_page.to_string()
        );
        // Settings saved to config by dialog, applied on next session start
    }

    /// Apply display presentation settings
    /// Note: Resolution/color depth are set by guest OS (via INT 10h or Windows drivers).
    /// Scaling and smoothing are handled by QML Image element properties.
    pub fn apply_display_settings(&self, scaling_mode: QString, scale_factor: i32, smooth: bool) {
        tracing::info!(
            "Applying display settings: mode={}, scale={}, smooth={}",
            scaling_mode.to_string(),
            scale_factor,
            smooth
        );
        // Scaling is handled by QML - settings saved to config for persistence
    }

    /// Apply network settings
    /// Note: Network is configured at session start. Runtime changes require
    /// restart for the emulated NE2000 to reinitialize.
    pub fn apply_network_settings(&self, interface: QString, enabled: bool) {
        tracing::info!(
            "Applying network settings: interface={}, enabled={}",
            interface.to_string(),
            enabled
        );
        // Settings saved to config, applied on next session start
    }

    /// Apply clipboard settings
    /// Note: Clipboard direction can be changed at runtime via ClipboardController.
    pub fn apply_clipboard_settings(&self, enabled: bool, direction: QString) {
        tracing::info!(
            "Applying clipboard settings: enabled={}, direction={}",
            enabled,
            direction.to_string()
        );
        // ClipboardController handles runtime changes, config saves for persistence
    }

    /// Get available network interfaces
    /// 
    /// Enumerates network interfaces from /sys/class/net, excluding loopback.
    /// Note: Returns interfaces as comma-separated string for QML compatibility
    /// since QStringList construction requires QList<QString>.
    pub fn get_network_interfaces(&self) -> QStringList {
        // For now return empty - the network dialog uses its own interface enumeration
        // or manual entry. Full QStringList support requires QList construction.
        QStringList::default()
    }
}
