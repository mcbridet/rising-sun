//! Drive mapping controller for filesystem redirection.
//!
//! Maps host directories to guest drive letters (E: through Z:).
//! Uses the kernel driver's FSD (Filesystem Redirection) subsystem.

use std::cell::RefCell;
use std::collections::HashMap;

use rising_sun_common::ioctl::{DriveMapping as IoctlDriveMapping, DriveLetter, drive_flags};
use rising_sun_common::ioctl::{sunpci_add_drive_map, sunpci_remove_drive_map, SUNPCI_MAX_PATH};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(i32, driver_fd)]
        #[qproperty(i32, mapping_count)]
        type DriveMappingController = super::DriveMappingControllerRust;

        /// Initialize with driver file descriptor
        #[qinvokable]
        fn init_mappings(self: Pin<&mut DriveMappingController>, fd: i32) -> bool;

        /// Add a drive mapping
        #[qinvokable]
        fn add_mapping(self: Pin<&mut DriveMappingController>, drive_letter: QString, host_path: QString, readonly: bool) -> bool;

        /// Remove a drive mapping
        #[qinvokable]
        fn remove_mapping(self: Pin<&mut DriveMappingController>, drive_letter: QString) -> bool;

        /// Apply all drive mappings to the driver
        #[qinvokable]
        fn apply_mappings(self: Pin<&mut DriveMappingController>) -> bool;

        /// Clear all mappings from the driver
        #[qinvokable]
        fn clear_mappings(self: Pin<&mut DriveMappingController>) -> bool;

        /// Get current mappings as JSON
        #[qinvokable]
        fn get_mappings_json(self: &DriveMappingController) -> QString;

        /// Load mappings from JSON
        #[qinvokable]
        fn load_mappings_json(self: Pin<&mut DriveMappingController>, json: QString) -> bool;

        /// Get default mappings (like original SunPCi)
        #[qinvokable]
        fn get_default_mappings_json(self: &DriveMappingController) -> QString;

        /// Check if a drive letter is valid (E-Z)
        #[qinvokable]
        fn is_valid_drive_letter(self: &DriveMappingController, letter: QString) -> bool;

        /// Get list of available (unmapped) drive letters
        #[qinvokable]
        fn get_available_letters(self: &DriveMappingController) -> QString;
    }

    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;

/// A single drive mapping entry
#[derive(Clone, Debug)]
pub struct DriveMapping {
    pub letter: char,
    pub host_path: String,
    pub readonly: bool,
    pub enabled: bool,
}

/// Rust implementation of the DriveMappingController
/// Based on analysis/05-filesystem-redirection.md
pub struct DriveMappingControllerRust {
    driver_fd: i32,
    mapping_count: i32,
    /// Current drive mappings
    mappings: RefCell<HashMap<char, DriveMapping>>,
}

impl Default for DriveMappingControllerRust {
    fn default() -> Self {
        Self {
            driver_fd: -1,
            mapping_count: 0,
            mappings: RefCell::new(HashMap::new()),
        }
    }
}

impl qobject::DriveMappingController {
    /// Initialize with driver file descriptor
    pub fn init_mappings(mut self: Pin<&mut Self>, fd: i32) -> bool {
        if fd < 0 {
            tracing::warn!("DriveMappingController: invalid driver fd");
            return false;
        }
        self.as_mut().set_driver_fd(fd);
        tracing::info!("DriveMappingController initialized with fd={}", fd);
        true
    }

    /// Add a drive mapping
    pub fn add_mapping(
        mut self: Pin<&mut Self>,
        drive_letter: QString,
        host_path: QString,
        readonly: bool,
    ) -> bool {
        let letter_str = drive_letter.to_string().to_uppercase();
        let letter = match parse_drive_letter(&letter_str) {
            Some(l) => l,
            None => {
                tracing::warn!("Invalid drive letter: {}", letter_str);
                return false;
            }
        };

        let path = host_path.to_string();
        
        // Expand ~ to home directory
        let expanded_path = if path.starts_with('~') {
            if let Some(home) = std::env::var("HOME").ok() {
                path.replacen('~', &home, 1)
            } else {
                path
            }
        } else {
            path
        };

        // Verify path exists
        if !std::path::Path::new(&expanded_path).exists() {
            tracing::warn!("Host path does not exist: {}", expanded_path);
            // Still allow adding - might be created later
        }

        let mapping = DriveMapping {
            letter,
            host_path: expanded_path,
            readonly,
            enabled: true,
        };

        self.mappings.borrow_mut().insert(letter, mapping);
        let count = self.mappings.borrow().len() as i32;
        self.as_mut().set_mapping_count(count);

        tracing::info!("Added drive mapping: {}: -> {}", letter, host_path.to_string());
        true
    }

    /// Remove a drive mapping
    pub fn remove_mapping(mut self: Pin<&mut Self>, drive_letter: QString) -> bool {
        let letter_str = drive_letter.to_string().to_uppercase();
        let letter = match parse_drive_letter(&letter_str) {
            Some(l) => l,
            None => {
                tracing::warn!("Invalid drive letter: {}", letter_str);
                return false;
            }
        };

        // Remove from our map
        let removed = self.mappings.borrow_mut().remove(&letter).is_some();
        
        if removed {
            let count = self.mappings.borrow().len() as i32;
            self.as_mut().set_mapping_count(count);

            // If driver is connected, also remove from driver
            if self.driver_fd >= 0 {
                let drive_letter_struct = DriveLetter {
                    letter: letter as u8,
                    _pad: [0; 3],
                };
                let result = unsafe {
                    sunpci_remove_drive_map(self.driver_fd, &drive_letter_struct)
                };
                if let Err(e) = result {
                    tracing::warn!("Failed to remove mapping from driver: {}", e);
                }
            }

            tracing::info!("Removed drive mapping: {}:", letter);
        }

        removed
    }

    /// Apply all drive mappings to the driver
    pub fn apply_mappings(self: Pin<&mut Self>) -> bool {
        if self.driver_fd < 0 {
            tracing::warn!("Cannot apply mappings: no driver connection");
            return false;
        }

        let mappings = self.mappings.borrow();
        let mut success = true;

        for mapping in mappings.values() {
            if !mapping.enabled {
                continue;
            }

            let mut ioctl_mapping = IoctlDriveMapping::default();
            ioctl_mapping.letter = mapping.letter as u8;
            ioctl_mapping.flags = if mapping.readonly { drive_flags::READONLY } else { 0 };
            
            // Copy path
            let path_bytes = mapping.host_path.as_bytes();
            let len = path_bytes.len().min(SUNPCI_MAX_PATH - 1);
            ioctl_mapping.path[..len].copy_from_slice(&path_bytes[..len]);
            ioctl_mapping.path[len] = 0;

            let result = unsafe {
                sunpci_add_drive_map(self.driver_fd, &ioctl_mapping)
            };

            match result {
                Ok(_) => {
                    tracing::info!("Applied mapping {}:", mapping.letter);
                }
                Err(e) => {
                    tracing::error!("Failed to apply mapping {}:: {}", mapping.letter, e);
                    success = false;
                }
            }
        }

        success
    }

    /// Clear all mappings from the driver
    pub fn clear_mappings(mut self: Pin<&mut Self>) -> bool {
        if self.driver_fd < 0 {
            // Just clear local state
            self.mappings.borrow_mut().clear();
            self.as_mut().set_mapping_count(0);
            return true;
        }

        let letters: Vec<char> = self.mappings.borrow().keys().copied().collect();
        
        for letter in letters {
            let drive_letter = DriveLetter {
                letter: letter as u8,
                _pad: [0; 3],
            };
            let _ = unsafe { sunpci_remove_drive_map(self.driver_fd, &drive_letter) };
        }

        self.mappings.borrow_mut().clear();
        self.as_mut().set_mapping_count(0);
        tracing::info!("Cleared all drive mappings");
        true
    }

    /// Get current mappings as JSON
    pub fn get_mappings_json(&self) -> QString {
        let mappings = self.mappings.borrow();
        
        let json_array: Vec<String> = mappings.values().map(|m| {
            format!(
                r#"{{"driveLetter":"{}:","hostPath":"{}","readonly":{},"enabled":{}}}"#,
                m.letter,
                m.host_path.replace('\\', "\\\\").replace('"', "\\\""),
                m.readonly,
                m.enabled
            )
        }).collect();

        QString::from(&format!("[{}]", json_array.join(",")))
    }

    /// Load mappings from JSON
    pub fn load_mappings_json(mut self: Pin<&mut Self>, json: QString) -> bool {
        let json_str = json.to_string();
        
        // Simple JSON parsing (for array of mapping objects)
        // Expected format: [{"driveLetter":"F:","hostPath":"/path","readonly":false,"enabled":true},...]
        
        self.mappings.borrow_mut().clear();

        // Very simple parsing - look for driveLetter and hostPath patterns
        for entry in json_str.split('}') {
            if let (Some(letter_start), Some(path_start)) = (
                entry.find("\"driveLetter\""),
                entry.find("\"hostPath\""),
            ) {
                // Extract drive letter
                let letter_part = &entry[letter_start..];
                if let Some(letter) = extract_json_string(letter_part, "driveLetter") {
                    if let Some(l) = parse_drive_letter(&letter) {
                        // Extract host path
                        let path_part = &entry[path_start..];
                        if let Some(path) = extract_json_string(path_part, "hostPath") {
                            let readonly = entry.contains("\"readonly\":true");
                            let enabled = !entry.contains("\"enabled\":false");

                            let mapping = DriveMapping {
                                letter: l,
                                host_path: path,
                                readonly,
                                enabled,
                            };
                            self.mappings.borrow_mut().insert(l, mapping);
                        }
                    }
                }
            }
        }

        let count = self.mappings.borrow().len() as i32;
        self.as_mut().set_mapping_count(count);
        tracing::debug!("Loaded {} mappings from JSON", count);
        true
    }

    /// Get default mappings (like original SunPCi autoexec.bat)
    /// From analysis/05-filesystem-redirection.md:
    /// - F: = $SUNPCIIHOME (/opt/SUNWspci)
    /// - H: = ~ (home directory)
    /// - R: = / (root filesystem)
    pub fn get_default_mappings_json(&self) -> QString {
        QString::from(
            r#"[
                {"driveLetter":"F:","hostPath":"/opt/SUNWspci","readonly":true,"enabled":true},
                {"driveLetter":"H:","hostPath":"~","readonly":false,"enabled":true},
                {"driveLetter":"R:","hostPath":"/","readonly":true,"enabled":false}
            ]"#,
        )
    }

    /// Check if a drive letter is valid (E-Z)
    pub fn is_valid_drive_letter(&self, letter: QString) -> bool {
        parse_drive_letter(&letter.to_string()).is_some()
    }

    /// Get list of available (unmapped) drive letters as comma-separated string
    pub fn get_available_letters(&self) -> QString {
        let mappings = self.mappings.borrow();
        let used: std::collections::HashSet<char> = mappings.keys().copied().collect();
        
        let available: Vec<String> = ('E'..='Z')
            .filter(|c| !used.contains(c))
            .map(|c| format!("{}:", c))
            .collect();

        QString::from(&available.join(","))
    }
}

/// Parse a drive letter string (e.g., "F:", "F", "f:") to a char
fn parse_drive_letter(s: &str) -> Option<char> {
    let s = s.trim().to_uppercase();
    let letter = s.chars().next()?;
    
    // Valid drive letters for mapping are E through Z
    // A-D are reserved (A/B = floppy, C/D = hard disk)
    if letter >= 'E' && letter <= 'Z' {
        Some(letter)
    } else {
        None
    }
}

/// Extract a JSON string value (very simple parser)
fn extract_json_string(s: &str, key: &str) -> Option<String> {
    let pattern = format!("\"{}\"", key);
    let start = s.find(&pattern)?;
    let after_key = &s[start + pattern.len()..];
    
    // Find the colon and opening quote
    let colon = after_key.find(':')?;
    let after_colon = &after_key[colon + 1..];
    let quote_start = after_colon.find('"')?;
    let after_quote = &after_colon[quote_start + 1..];
    
    // Find the closing quote (handle escaped quotes)
    let mut end = 0;
    let mut escaped = false;
    for (i, c) in after_quote.chars().enumerate() {
        if escaped {
            escaped = false;
            continue;
        }
        if c == '\\' {
            escaped = true;
            continue;
        }
        if c == '"' {
            end = i;
            break;
        }
    }
    
    Some(after_quote[..end].to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_drive_letter() {
        assert_eq!(parse_drive_letter("F:"), Some('F'));
        assert_eq!(parse_drive_letter("F"), Some('F'));
        assert_eq!(parse_drive_letter("f:"), Some('F'));
        assert_eq!(parse_drive_letter("Z:"), Some('Z'));
        assert_eq!(parse_drive_letter("E:"), Some('E'));
        
        // Reserved letters
        assert_eq!(parse_drive_letter("A:"), None);
        assert_eq!(parse_drive_letter("C:"), None);
        assert_eq!(parse_drive_letter("D:"), None);
        
        // Invalid
        assert_eq!(parse_drive_letter("1:"), None);
        assert_eq!(parse_drive_letter(""), None);
    }

    #[test]
    fn test_extract_json_string() {
        let json = r#"{"driveLetter":"F:","hostPath":"/opt/SUNWspci"}"#;
        assert_eq!(extract_json_string(json, "driveLetter"), Some("F:".to_string()));
        assert_eq!(extract_json_string(json, "hostPath"), Some("/opt/SUNWspci".to_string()));
    }
}
