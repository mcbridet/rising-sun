//! Configuration storage for persistent application state.
//!
//! Stores user preferences and session configuration in a TOML file
//! located at ~/.config/rising-sun/config.toml (or XDG_CONFIG_HOME).

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

/// Main configuration structure containing all persistent settings
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct AppConfig {
    /// General application settings
    pub general: GeneralConfig,
    /// Display/presentation settings
    pub display: DisplayConfig,
    /// Keyboard settings
    pub keyboard: KeyboardConfig,
    /// Mouse settings
    pub mouse: MouseConfig,
    /// Clipboard settings
    pub clipboard: ClipboardConfig,
    /// Network adapter settings
    pub network: NetworkConfig,
    /// Storage devices (disks, CD-ROM, floppy)
    pub storage: StorageConfig,
    /// Host directory to guest drive letter mappings
    pub drive_mappings: Vec<DriveMapping>,
    /// Recently used files
    pub recent: RecentFiles,
}

/// General application settings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct GeneralConfig {
    /// Start session automatically on application launch
    pub auto_start: bool,
    /// Save session state on exit
    pub save_state_on_exit: bool,
    /// Confirm before closing while session is running
    pub confirm_on_close: bool,
    /// Show status bar
    pub show_status_bar: bool,
    /// Remember window position and size
    pub remember_window_geometry: bool,
    /// Window X position
    pub window_x: Option<i32>,
    /// Window Y position
    pub window_y: Option<i32>,
    /// Window width
    pub window_width: Option<u32>,
    /// Window height
    pub window_height: Option<u32>,
}

impl Default for GeneralConfig {
    fn default() -> Self {
        Self {
            auto_start: false,
            save_state_on_exit: true,
            confirm_on_close: true,
            show_status_bar: true,
            remember_window_geometry: true,
            window_x: None,
            window_y: None,
            window_width: None,
            window_height: None,
        }
    }
}

/// Display presentation settings (host-side only)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct DisplayConfig {
    /// Scaling mode for the guest display
    pub scaling_mode: ScalingMode,
    /// Maintain aspect ratio when scaling
    pub maintain_aspect_ratio: bool,
    /// Use integer scaling only (pixel-perfect)
    pub integer_scaling: bool,
    /// Apply CRT scanline effect
    pub scanline_effect: bool,
    /// Scanline intensity (0.0 - 1.0)
    pub scanline_intensity: f32,
    /// Start in fullscreen mode
    pub start_fullscreen: bool,
    /// Hide menu bar in fullscreen
    pub fullscreen_hide_menu: bool,
}

impl Default for DisplayConfig {
    fn default() -> Self {
        Self {
            scaling_mode: ScalingMode::Fit,
            maintain_aspect_ratio: true,
            integer_scaling: false,
            scanline_effect: false,
            scanline_intensity: 0.3,
            start_fullscreen: false,
            fullscreen_hide_menu: true,
        }
    }
}

/// Display scaling modes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum ScalingMode {
    /// No scaling (1:1 pixels)
    None,
    /// Scale to fit window while maintaining aspect ratio
    #[default]
    Fit,
    /// Stretch to fill window (may distort)
    Stretch,
    /// Fixed scale factor
    Fixed(u32),
}

/// Keyboard settings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct KeyboardConfig {
    /// Keyboard layout (e.g., "us", "uk", "de")
    pub layout: String,
    /// DOS code page (e.g., "437", "850")
    pub code_page: String,
    /// Key used to release keyboard capture
    pub release_key: String,
    /// Automatically capture keyboard when window focused
    pub auto_capture: bool,
    /// Synchronize Caps Lock state with host
    pub sync_caps_lock: bool,
    /// Synchronize Num Lock state with host
    pub sync_num_lock: bool,
    /// Synchronize Scroll Lock state with host
    pub sync_scroll_lock: bool,
}

impl Default for KeyboardConfig {
    fn default() -> Self {
        Self {
            layout: "us".to_string(),
            code_page: "437".to_string(),
            release_key: "Right Ctrl".to_string(),
            auto_capture: false,
            sync_caps_lock: true,
            sync_num_lock: true,
            sync_scroll_lock: true,
        }
    }
}

/// Mouse settings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct MouseConfig {
    /// Mouse protocol
    pub protocol: MouseProtocol,
    /// Capture mode
    pub capture_mode: MouseCaptureMode,
    /// Swap left and right buttons
    pub swap_buttons: bool,
    /// Simulate middle button with left+right click
    pub simulate_middle_button: bool,
}

impl Default for MouseConfig {
    fn default() -> Self {
        Self {
            protocol: MouseProtocol::Ps2,
            capture_mode: MouseCaptureMode::ClickToCapture,
            swap_buttons: false,
            simulate_middle_button: false,
        }
    }
}

/// Mouse protocol types
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum MouseProtocol {
    #[default]
    Ps2,
    Serial,
    Absolute,
}

/// Mouse capture modes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum MouseCaptureMode {
    /// Click inside window to capture
    #[default]
    ClickToCapture,
    /// Capture when mouse enters window
    HoverToCapture,
    /// Never capture (for seamless integration)
    Seamless,
}

/// Clipboard sharing settings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct ClipboardConfig {
    /// Enable clipboard sharing
    pub enabled: bool,
    /// Direction of clipboard sharing
    pub direction: ClipboardDirection,
    /// Share plain text
    pub share_text: bool,
    /// Share rich text (RTF)
    pub share_rich_text: bool,
    /// Share images
    pub share_images: bool,
    /// Share file references
    pub share_files: bool,
}

impl Default for ClipboardConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            direction: ClipboardDirection::Bidirectional,
            share_text: true,
            share_rich_text: true,
            share_images: true,
            share_files: false,
        }
    }
}

/// Clipboard sharing direction
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum ClipboardDirection {
    #[default]
    Bidirectional,
    HostToGuest,
    GuestToHost,
}

/// Network adapter settings
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct NetworkConfig {
    /// Enable network adapter
    pub enabled: bool,
    /// Host network interface to bridge
    pub host_interface: String,
    /// MAC address (empty = auto-generate)
    pub mac_address: String,
    /// IRQ number
    pub irq: u8,
    /// Enable promiscuous mode
    pub promiscuous: bool,
}

impl Default for NetworkConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            host_interface: String::new(),
            mac_address: String::new(),
            irq: 10,
            promiscuous: false,
        }
    }
}

/// Storage device configuration
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct StorageConfig {
    /// Primary hard disk (C:)
    pub primary_disk: Option<DiskConfig>,
    /// Secondary hard disk (D:)
    pub secondary_disk: Option<DiskConfig>,
    /// CD-ROM drive
    pub cdrom: CdromConfig,
    /// Floppy drive A:
    pub floppy_a: FloppyConfig,
    /// Floppy drive B:
    pub floppy_b: FloppyConfig,
}

/// Hard disk configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiskConfig {
    /// Path to disk image file
    pub path: PathBuf,
    /// Whether this disk is bootable
    pub bootable: bool,
}

/// CD-ROM drive configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct CdromConfig {
    /// Currently mounted ISO path (if any)
    pub mounted_iso: Option<PathBuf>,
    /// Auto-mount this ISO on session start
    pub auto_mount: bool,
    /// Boot from CD-ROM (El Torito)
    pub boot_from_cd: bool,
}

impl Default for CdromConfig {
    fn default() -> Self {
        Self {
            mounted_iso: None,
            auto_mount: true,
            boot_from_cd: false,
        }
    }
}

/// Floppy drive configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct FloppyConfig {
    /// Currently mounted floppy image path (if any)
    pub mounted_image: Option<PathBuf>,
    /// Auto-mount this image on session start
    pub auto_mount: bool,
    /// Write protect the floppy
    pub write_protected: bool,
}

impl Default for FloppyConfig {
    fn default() -> Self {
        Self {
            mounted_image: None,
            auto_mount: true,
            write_protected: false,
        }
    }
}

/// Host directory to guest drive letter mapping
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriveMapping {
    /// Guest drive letter (e.g., "F:")
    pub drive_letter: String,
    /// Host directory path
    pub host_path: PathBuf,
    /// Description/label
    pub description: String,
    /// Whether this mapping is enabled
    pub enabled: bool,
}

impl Default for DriveMapping {
    fn default() -> Self {
        Self {
            drive_letter: "F:".to_string(),
            host_path: PathBuf::new(),
            description: String::new(),
            enabled: true,
        }
    }
}

/// Recently used files for quick access
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct RecentFiles {
    /// Recently used disk images
    pub disk_images: Vec<PathBuf>,
    /// Recently used ISO files
    pub iso_files: Vec<PathBuf>,
    /// Recently used floppy images
    pub floppy_images: Vec<PathBuf>,
    /// Maximum number of recent files to remember per category
    #[serde(default = "default_max_recent")]
    pub max_recent: usize,
}

fn default_max_recent() -> usize {
    10
}

impl RecentFiles {
    /// Add a disk image to recent files
    pub fn add_disk_image(&mut self, path: PathBuf) {
        self.add_to_list(&mut self.disk_images.clone(), path);
    }

    /// Add an ISO to recent files
    pub fn add_iso(&mut self, path: PathBuf) {
        self.add_to_list(&mut self.iso_files.clone(), path);
    }

    /// Add a floppy image to recent files
    pub fn add_floppy_image(&mut self, path: PathBuf) {
        self.add_to_list(&mut self.floppy_images.clone(), path);
    }

    fn add_to_list(&mut self, list: &mut Vec<PathBuf>, path: PathBuf) {
        // Remove if already present
        list.retain(|p| p != &path);
        // Add to front
        list.insert(0, path);
        // Trim to max size
        list.truncate(self.max_recent);
    }
}

impl AppConfig {
    /// Get the default configuration directory
    pub fn config_dir() -> PathBuf {
        if let Ok(xdg_config) = std::env::var("XDG_CONFIG_HOME") {
            PathBuf::from(xdg_config).join("rising-sun")
        } else if let Ok(home) = std::env::var("HOME") {
            PathBuf::from(home).join(".config").join("rising-sun")
        } else {
            PathBuf::from(".config").join("rising-sun")
        }
    }

    /// Get the default configuration file path
    pub fn config_file() -> PathBuf {
        Self::config_dir().join("config.toml")
    }

    /// Get the default data directory (for session state, etc.)
    pub fn data_dir() -> PathBuf {
        if let Ok(xdg_data) = std::env::var("XDG_DATA_HOME") {
            PathBuf::from(xdg_data).join("rising-sun")
        } else if let Ok(home) = std::env::var("HOME") {
            PathBuf::from(home).join(".local").join("share").join("rising-sun")
        } else {
            PathBuf::from(".local").join("share").join("rising-sun")
        }
    }

    /// Create default drive mappings like original SunPCI
    /// Note: By default, no mappings are configured. This function
    /// provides suggested mappings that can be added by the user.
    pub fn suggested_drive_mappings() -> Vec<DriveMapping> {
        let home = std::env::var("HOME").unwrap_or_else(|_| "/home".to_string());
        vec![
            DriveMapping {
                drive_letter: "F:".to_string(),
                host_path: PathBuf::from("/opt/rising-sun"),
                description: "Rising Sun Installation".to_string(),
                enabled: true,
            },
            DriveMapping {
                drive_letter: "H:".to_string(),
                host_path: PathBuf::from(&home),
                description: "Home Directory".to_string(),
                enabled: true,
            },
            DriveMapping {
                drive_letter: "R:".to_string(),
                host_path: PathBuf::from("/"),
                description: "Root Filesystem".to_string(),
                enabled: false,
            },
        ]
    }
}
