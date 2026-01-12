//! Configuration manager Qt bridge for accessing persistent settings from QML.

use rising_sun_common::{AppConfig, load_config, save_config, DiskConfig, DriveMapping};
use std::path::PathBuf;
use std::cell::RefCell;

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        type ConfigManager = super::ConfigManagerRust;

        // General settings getters
        #[qinvokable]
        fn get_auto_start(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn get_save_state_on_exit(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn get_confirm_on_close(self: &ConfigManager) -> bool;

        // General settings setters
        #[qinvokable]
        fn set_auto_start_value(self: &ConfigManager, value: bool);
        #[qinvokable]
        fn set_save_state_on_exit_value(self: &ConfigManager, value: bool);
        #[qinvokable]
        fn set_confirm_on_close_value(self: &ConfigManager, value: bool);

        // Display settings
        #[qinvokable]
        fn get_maintain_aspect_ratio(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn set_maintain_aspect_ratio_value(self: &ConfigManager, value: bool);
        #[qinvokable]
        fn get_integer_scaling(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn set_integer_scaling_value(self: &ConfigManager, value: bool);
        #[qinvokable]
        fn get_scanline_effect(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn set_scanline_effect_value(self: &ConfigManager, value: bool);

        // Keyboard settings
        #[qinvokable]
        fn get_keyboard_layout(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_keyboard_layout_value(self: &ConfigManager, value: QString);
        #[qinvokable]
        fn get_code_page(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_code_page_value(self: &ConfigManager, value: QString);

        // Storage paths
        #[qinvokable]
        fn get_primary_disk_path(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_primary_disk(self: &ConfigManager, path: QString);
        #[qinvokable]
        fn get_secondary_disk_path(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_secondary_disk(self: &ConfigManager, path: QString);
        #[qinvokable]
        fn get_cdrom_iso_path(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_cdrom_iso(self: &ConfigManager, path: QString);
        #[qinvokable]
        fn get_floppy_a_path(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_floppy_a(self: &ConfigManager, path: QString);
        #[qinvokable]
        fn get_floppy_b_path(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_floppy_b(self: &ConfigManager, path: QString);

        // Network settings
        #[qinvokable]
        fn get_network_enabled(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn set_network_enabled_value(self: &ConfigManager, value: bool);
        #[qinvokable]
        fn get_network_interface(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_network_interface_value(self: &ConfigManager, value: QString);
        #[qinvokable]
        fn get_mac_address(self: &ConfigManager) -> QString;
        #[qinvokable]
        fn set_mac_address_value(self: &ConfigManager, value: QString);

        // Clipboard settings
        #[qinvokable]
        fn get_clipboard_enabled(self: &ConfigManager) -> bool;
        #[qinvokable]
        fn set_clipboard_enabled_value(self: &ConfigManager, value: bool);

        // Drive mappings
        #[qinvokable]
        fn drive_mapping_count(self: &ConfigManager) -> i32;
        #[qinvokable]
        fn get_drive_mapping_letter(self: &ConfigManager, index: i32) -> QString;
        #[qinvokable]
        fn get_drive_mapping_path(self: &ConfigManager, index: i32) -> QString;
        #[qinvokable]
        fn get_drive_mapping_description(self: &ConfigManager, index: i32) -> QString;
        #[qinvokable]
        fn get_drive_mapping_enabled(self: &ConfigManager, index: i32) -> bool;
        #[qinvokable]
        fn add_drive_mapping(self: &ConfigManager, letter: QString, path: QString, description: QString);
        #[qinvokable]
        fn remove_drive_mapping(self: &ConfigManager, letter: QString);
        #[qinvokable]
        fn set_drive_mapping_enabled(self: &ConfigManager, letter: QString, enabled: bool);

        // Recent files
        #[qinvokable]
        fn recent_disk_count(self: &ConfigManager) -> i32;
        #[qinvokable]
        fn get_recent_disk_path(self: &ConfigManager, index: i32) -> QString;
        #[qinvokable]
        fn recent_iso_count(self: &ConfigManager) -> i32;
        #[qinvokable]
        fn get_recent_iso_path(self: &ConfigManager, index: i32) -> QString;
        #[qinvokable]
        fn recent_floppy_count(self: &ConfigManager) -> i32;
        #[qinvokable]
        fn get_recent_floppy_path(self: &ConfigManager, index: i32) -> QString;

        // Load and save
        #[qinvokable]
        fn load(self: &ConfigManager);
        #[qinvokable]
        fn save(self: &ConfigManager);
    }
}

use cxx_qt_lib::QString;

/// Rust implementation of the ConfigManager
pub struct ConfigManagerRust {
    config: RefCell<AppConfig>,
}

impl Default for ConfigManagerRust {
    fn default() -> Self {
        // Start with default config - load() should be called from QML
        Self {
            config: RefCell::new(AppConfig::default()),
        }
    }
}

impl qobject::ConfigManager {
    // General settings
    fn get_auto_start(&self) -> bool {
        self.config.borrow().general.auto_start
    }
    fn set_auto_start_value(&self, value: bool) {
        self.config.borrow_mut().general.auto_start = value;
    }
    fn get_save_state_on_exit(&self) -> bool {
        self.config.borrow().general.save_state_on_exit
    }
    fn set_save_state_on_exit_value(&self, value: bool) {
        self.config.borrow_mut().general.save_state_on_exit = value;
    }
    fn get_confirm_on_close(&self) -> bool {
        self.config.borrow().general.confirm_on_close
    }
    fn set_confirm_on_close_value(&self, value: bool) {
        self.config.borrow_mut().general.confirm_on_close = value;
    }

    // Display settings
    fn get_maintain_aspect_ratio(&self) -> bool {
        self.config.borrow().display.maintain_aspect_ratio
    }
    fn set_maintain_aspect_ratio_value(&self, value: bool) {
        self.config.borrow_mut().display.maintain_aspect_ratio = value;
    }
    fn get_integer_scaling(&self) -> bool {
        self.config.borrow().display.integer_scaling
    }
    fn set_integer_scaling_value(&self, value: bool) {
        self.config.borrow_mut().display.integer_scaling = value;
    }
    fn get_scanline_effect(&self) -> bool {
        self.config.borrow().display.scanline_effect
    }
    fn set_scanline_effect_value(&self, value: bool) {
        self.config.borrow_mut().display.scanline_effect = value;
    }

    // Keyboard settings
    fn get_keyboard_layout(&self) -> QString {
        QString::from(&self.config.borrow().keyboard.layout)
    }
    fn set_keyboard_layout_value(&self, value: QString) {
        self.config.borrow_mut().keyboard.layout = value.to_string();
    }
    fn get_code_page(&self) -> QString {
        QString::from(&self.config.borrow().keyboard.code_page)
    }
    fn set_code_page_value(&self, value: QString) {
        self.config.borrow_mut().keyboard.code_page = value.to_string();
    }

    // Storage paths
    fn get_primary_disk_path(&self) -> QString {
        self.config
            .borrow()
            .storage
            .primary_disk
            .as_ref()
            .map(|d| QString::from(d.path.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn set_primary_disk(&self, path: QString) {
        let path_str = path.to_string();
        let mut config = self.config.borrow_mut();
        if path_str.is_empty() {
            config.storage.primary_disk = None;
        } else {
            config.storage.primary_disk = Some(DiskConfig {
                path: PathBuf::from(&path_str),
                bootable: true,
            });
            // Add to recent files
            config.recent.disk_images.retain(|p| p.to_string_lossy() != path_str);
            config.recent.disk_images.insert(0, PathBuf::from(&path_str));
            config.recent.disk_images.truncate(10);
        }
    }

    fn get_secondary_disk_path(&self) -> QString {
        self.config
            .borrow()
            .storage
            .secondary_disk
            .as_ref()
            .map(|d| QString::from(d.path.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn set_secondary_disk(&self, path: QString) {
        let path_str = path.to_string();
        let mut config = self.config.borrow_mut();
        if path_str.is_empty() {
            config.storage.secondary_disk = None;
        } else {
            config.storage.secondary_disk = Some(DiskConfig {
                path: PathBuf::from(&path_str),
                bootable: false,
            });
        }
    }

    fn get_cdrom_iso_path(&self) -> QString {
        self.config
            .borrow()
            .storage
            .cdrom
            .mounted_iso
            .as_ref()
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn set_cdrom_iso(&self, path: QString) {
        let path_str = path.to_string();
        let mut config = self.config.borrow_mut();
        if path_str.is_empty() {
            config.storage.cdrom.mounted_iso = None;
        } else {
            config.storage.cdrom.mounted_iso = Some(PathBuf::from(&path_str));
            // Add to recent files
            config.recent.iso_files.retain(|p| p.to_string_lossy() != path_str);
            config.recent.iso_files.insert(0, PathBuf::from(&path_str));
            config.recent.iso_files.truncate(10);
        }
    }

    fn get_floppy_a_path(&self) -> QString {
        self.config
            .borrow()
            .storage
            .floppy_a
            .mounted_image
            .as_ref()
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn set_floppy_a(&self, path: QString) {
        let path_str = path.to_string();
        let mut config = self.config.borrow_mut();
        if path_str.is_empty() {
            config.storage.floppy_a.mounted_image = None;
        } else {
            config.storage.floppy_a.mounted_image = Some(PathBuf::from(&path_str));
            // Add to recent files
            config.recent.floppy_images.retain(|p| p.to_string_lossy() != path_str);
            config.recent.floppy_images.insert(0, PathBuf::from(&path_str));
            config.recent.floppy_images.truncate(10);
        }
    }

    fn get_floppy_b_path(&self) -> QString {
        self.config
            .borrow()
            .storage
            .floppy_b
            .mounted_image
            .as_ref()
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn set_floppy_b(&self, path: QString) {
        let path_str = path.to_string();
        let mut config = self.config.borrow_mut();
        if path_str.is_empty() {
            config.storage.floppy_b.mounted_image = None;
        } else {
            config.storage.floppy_b.mounted_image = Some(PathBuf::from(&path_str));
        }
    }

    // Network settings
    fn get_network_enabled(&self) -> bool {
        self.config.borrow().network.enabled
    }
    fn set_network_enabled_value(&self, value: bool) {
        self.config.borrow_mut().network.enabled = value;
    }
    fn get_network_interface(&self) -> QString {
        QString::from(&self.config.borrow().network.host_interface)
    }
    fn set_network_interface_value(&self, value: QString) {
        self.config.borrow_mut().network.host_interface = value.to_string();
    }
    fn get_mac_address(&self) -> QString {
        QString::from(&self.config.borrow().network.mac_address)
    }
    fn set_mac_address_value(&self, value: QString) {
        self.config.borrow_mut().network.mac_address = value.to_string();
    }

    // Clipboard settings
    fn get_clipboard_enabled(&self) -> bool {
        self.config.borrow().clipboard.enabled
    }
    fn set_clipboard_enabled_value(&self, value: bool) {
        self.config.borrow_mut().clipboard.enabled = value;
    }

    // Drive mappings
    fn drive_mapping_count(&self) -> i32 {
        self.config.borrow().drive_mappings.len() as i32
    }
    fn get_drive_mapping_letter(&self, index: i32) -> QString {
        self.config
            .borrow()
            .drive_mappings
            .get(index as usize)
            .map(|m| QString::from(&m.drive_letter))
            .unwrap_or_default()
    }
    fn get_drive_mapping_path(&self, index: i32) -> QString {
        self.config
            .borrow()
            .drive_mappings
            .get(index as usize)
            .map(|m| QString::from(m.host_path.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn get_drive_mapping_description(&self, index: i32) -> QString {
        self.config
            .borrow()
            .drive_mappings
            .get(index as usize)
            .map(|m| QString::from(&m.description))
            .unwrap_or_default()
    }
    fn get_drive_mapping_enabled(&self, index: i32) -> bool {
        self.config
            .borrow()
            .drive_mappings
            .get(index as usize)
            .map(|m| m.enabled)
            .unwrap_or(false)
    }
    fn add_drive_mapping(&self, letter: QString, path: QString, description: QString) {
        let mapping = DriveMapping {
            drive_letter: letter.to_string(),
            host_path: PathBuf::from(path.to_string()),
            description: description.to_string(),
            enabled: true,
        };
        let letter_str = letter.to_string();
        let mut config = self.config.borrow_mut();
        config.drive_mappings.retain(|m| m.drive_letter != letter_str);
        config.drive_mappings.push(mapping);
    }
    fn remove_drive_mapping(&self, letter: QString) {
        let letter_str = letter.to_string();
        self.config.borrow_mut().drive_mappings.retain(|m| m.drive_letter != letter_str);
    }
    fn set_drive_mapping_enabled(&self, letter: QString, enabled: bool) {
        let letter_str = letter.to_string();
        let mut config = self.config.borrow_mut();
        if let Some(mapping) = config.drive_mappings.iter_mut().find(|m| m.drive_letter == letter_str) {
            mapping.enabled = enabled;
        }
    }

    // Recent files
    fn recent_disk_count(&self) -> i32 {
        self.config.borrow().recent.disk_images.len() as i32
    }
    fn get_recent_disk_path(&self, index: i32) -> QString {
        self.config
            .borrow()
            .recent
            .disk_images
            .get(index as usize)
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn recent_iso_count(&self) -> i32 {
        self.config.borrow().recent.iso_files.len() as i32
    }
    fn get_recent_iso_path(&self, index: i32) -> QString {
        self.config
            .borrow()
            .recent
            .iso_files
            .get(index as usize)
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }
    fn recent_floppy_count(&self) -> i32 {
        self.config.borrow().recent.floppy_images.len() as i32
    }
    fn get_recent_floppy_path(&self, index: i32) -> QString {
        self.config
            .borrow()
            .recent
            .floppy_images
            .get(index as usize)
            .map(|p| QString::from(p.to_string_lossy().as_ref()))
            .unwrap_or_default()
    }

    // Load and save
    fn load(&self) {
        match load_config() {
            Ok(config) => {
                *self.config.borrow_mut() = config;
                tracing::info!("Configuration loaded from {:?}", AppConfig::config_file());
            }
            Err(e) => {
                tracing::error!("Failed to load configuration: {}", e);
            }
        }
    }

    fn save(&self) {
        if let Err(e) = save_config(&self.config.borrow()) {
            tracing::error!("Failed to save configuration: {}", e);
        } else {
            tracing::info!("Configuration saved to {:?}", AppConfig::config_file());
        }
    }
}
