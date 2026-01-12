//! Clipboard controller Qt bridge for clipboard synchronization.
//!
//! This module handles:
//! - Monitoring host clipboard changes → sending to guest
//! - Polling guest clipboard → updating host clipboard
//! - Bidirectional clipboard sync with direction control

use std::cell::RefCell;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use rising_sun_common::ioctl::{Clipboard, SUNPCI_MAX_CLIPBOARD, clipboard_format};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
        
        include!(<QtGui/QGuiApplication>);
        include!(<QtGui/QClipboard>);
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, clipboard_enabled)]
        #[qproperty(bool, host_to_guest)]
        #[qproperty(bool, guest_to_host)]
        #[qproperty(i32, driver_fd)]
        #[qproperty(QString, last_host_text)]
        #[qproperty(QString, last_guest_text)]
        #[qproperty(i32, host_to_guest_count)]
        #[qproperty(i32, guest_to_host_count)]
        #[qproperty(QString, status_text)]
        type ClipboardController = super::ClipboardControllerRust;

        /// Initialize clipboard controller with driver file descriptor
        #[qinvokable]
        fn init_clipboard(self: Pin<&mut ClipboardController>, fd: i32) -> bool;

        /// Enable or disable clipboard sync
        #[qinvokable]
        fn set_enabled(self: Pin<&mut ClipboardController>, enabled: bool);

        /// Set clipboard direction (both, host_to_guest, guest_to_host)
        #[qinvokable]
        fn set_direction(self: Pin<&mut ClipboardController>, direction: QString);

        /// Called when host clipboard changes (from QML clipboard monitoring)
        #[qinvokable]
        fn on_host_clipboard_changed(self: Pin<&mut ClipboardController>, text: QString);

        /// Poll guest clipboard and update host if changed
        #[qinvokable]
        fn poll_guest_clipboard(self: Pin<&mut ClipboardController>);

        /// Send text to guest clipboard
        #[qinvokable]
        fn send_to_guest(self: Pin<&mut ClipboardController>, text: QString) -> bool;

        /// Get text from guest clipboard
        #[qinvokable]
        fn get_from_guest(self: Pin<&mut ClipboardController>) -> QString;

        /// Get clipboard statistics
        #[qinvokable]
        fn get_stats(self: &ClipboardController) -> QString;

        /// Signal emitted when guest clipboard has new content for host
        #[qsignal]
        fn guest_clipboard_changed(self: Pin<&mut ClipboardController>, text: QString);

        /// Signal emitted when clipboard sync status changes
        #[qsignal]
        fn status_changed(self: Pin<&mut ClipboardController>, status: QString);
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;
use rising_sun_common::ioctl::{sunpci_get_clipboard, sunpci_set_clipboard};

/// Rust implementation of the ClipboardController
pub struct ClipboardControllerRust {
    /// Whether clipboard sync is enabled
    clipboard_enabled: bool,
    /// Whether host→guest transfer is enabled
    host_to_guest: bool,
    /// Whether guest→host transfer is enabled
    guest_to_host: bool,
    /// Driver file descriptor
    driver_fd: i32,
    /// Last text sent from host (to avoid loops)
    last_host_text: QString,
    /// Last text received from guest (to avoid loops)
    last_guest_text: QString,
    /// Count of host→guest transfers
    host_to_guest_count: i32,
    /// Count of guest→host transfers
    guest_to_host_count: i32,
    /// Current status text
    status_text: QString,
    /// Internal: last host clipboard hash (to detect changes)
    last_host_hash: RefCell<u64>,
    /// Internal: last guest clipboard hash (to detect changes)
    last_guest_hash: RefCell<u64>,
    /// Internal: whether we're currently updating clipboard (to prevent recursion)
    updating: Arc<AtomicBool>,
}

impl Default for ClipboardControllerRust {
    fn default() -> Self {
        Self {
            clipboard_enabled: true,
            host_to_guest: true,
            guest_to_host: true,
            driver_fd: -1,
            last_host_text: QString::from(""),
            last_guest_text: QString::from(""),
            host_to_guest_count: 0,
            guest_to_host_count: 0,
            status_text: QString::from("Clipboard disabled"),
            last_host_hash: RefCell::new(0),
            last_guest_hash: RefCell::new(0),
            updating: Arc::new(AtomicBool::new(false)),
        }
    }
}

/// Simple hash for clipboard text comparison
fn hash_text(text: &str) -> u64 {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let mut hasher = DefaultHasher::new();
    text.hash(&mut hasher);
    hasher.finish()
}

impl qobject::ClipboardController {
    /// Initialize clipboard controller with driver file descriptor
    pub fn init_clipboard(mut self: Pin<&mut Self>, fd: i32) -> bool {
        if fd < 0 {
            tracing::warn!("ClipboardController: invalid driver fd");
            self.as_mut().set_status_text(QString::from("No driver connection"));
            return false;
        }

        self.as_mut().set_driver_fd(fd);
        
        if self.clipboard_enabled {
            self.as_mut().set_status_text(QString::from("Clipboard ready"));
        }
        
        tracing::info!("ClipboardController initialized with fd={}", fd);
        true
    }

    /// Enable or disable clipboard sync
    pub fn set_enabled(mut self: Pin<&mut Self>, enabled: bool) {
        self.as_mut().set_clipboard_enabled(enabled);
        
        if enabled {
            self.as_mut().set_status_text(QString::from("Clipboard enabled"));
            tracing::info!("Clipboard sync enabled");
        } else {
            self.as_mut().set_status_text(QString::from("Clipboard disabled"));
            tracing::info!("Clipboard sync disabled");
        }
    }

    /// Set clipboard direction
    pub fn set_direction(mut self: Pin<&mut Self>, direction: QString) {
        let dir = direction.to_string();
        match dir.as_str() {
            "bidirectional" | "both" => {
                self.as_mut().set_host_to_guest(true);
                self.as_mut().set_guest_to_host(true);
                tracing::info!("Clipboard direction: bidirectional");
            }
            "host_to_guest" | "hostToGuest" => {
                self.as_mut().set_host_to_guest(true);
                self.as_mut().set_guest_to_host(false);
                tracing::info!("Clipboard direction: host to guest only");
            }
            "guest_to_host" | "guestToHost" => {
                self.as_mut().set_host_to_guest(false);
                self.as_mut().set_guest_to_host(true);
                tracing::info!("Clipboard direction: guest to host only");
            }
            _ => {
                tracing::warn!("Unknown clipboard direction: {}", dir);
            }
        }
    }

    /// Called when host clipboard changes
    pub fn on_host_clipboard_changed(mut self: Pin<&mut Self>, text: QString) {
        // Check if enabled and allowed
        if !self.clipboard_enabled || !self.host_to_guest {
            return;
        }

        // Check for recursion (we're updating the clipboard ourselves)
        if self.updating.load(Ordering::SeqCst) {
            return;
        }

        let text_str = text.to_string();
        
        // Check if actually different (by hash to avoid storing large strings)
        let new_hash = hash_text(&text_str);
        let old_hash = *self.last_host_hash.borrow();
        
        if new_hash == old_hash {
            return; // No change
        }

        // Empty clipboard is not useful to sync
        if text_str.is_empty() {
            return;
        }

        tracing::debug!("Host clipboard changed: {} bytes", text_str.len());
        
        // Update hash
        *self.last_host_hash.borrow_mut() = new_hash;

        // Send to guest
        if self.send_to_guest_internal(&text_str) {
            self.as_mut().set_last_host_text(QString::from(&text_str[..text_str.len().min(100)]));
            let count = self.host_to_guest_count + 1;
            self.as_mut().set_host_to_guest_count(count);
            tracing::debug!("Sent clipboard to guest ({} bytes)", text_str.len());
        }
    }

    /// Poll guest clipboard and update host if changed
    pub fn poll_guest_clipboard(mut self: Pin<&mut Self>) {
        // Check if enabled and allowed
        if !self.clipboard_enabled || !self.guest_to_host {
            return;
        }

        if self.driver_fd < 0 {
            return;
        }

        // Prevent recursion
        if self.updating.swap(true, Ordering::SeqCst) {
            return;
        }

        // Get clipboard from guest
        let result = self.get_from_guest_internal();
        
        self.updating.store(false, Ordering::SeqCst);

        if let Some(text) = result {
            if text.is_empty() {
                return;
            }

            // Check if different from last guest clipboard
            let new_hash = hash_text(&text);
            let old_hash = *self.last_guest_hash.borrow();

            if new_hash == old_hash {
                return; // No change
            }

            // Also check it's not the same as what we just sent TO guest
            let host_hash = *self.last_host_hash.borrow();
            if new_hash == host_hash {
                return; // This is our own clipboard echoing back
            }

            *self.last_guest_hash.borrow_mut() = new_hash;

            tracing::debug!("Guest clipboard changed: {} bytes", text.len());
            
            let preview = text[..text.len().min(100)].to_string();
            self.as_mut().set_last_guest_text(QString::from(&preview));
            
            let count = self.guest_to_host_count + 1;
            self.as_mut().set_guest_to_host_count(count);

            // Emit signal for QML to update host clipboard
            let text_qstring = QString::from(&text);
            self.as_mut().guest_clipboard_changed(text_qstring);
        }
    }

    /// Send text to guest clipboard (callable from QML)
    pub fn send_to_guest(self: Pin<&mut Self>, text: QString) -> bool {
        if self.driver_fd < 0 {
            return false;
        }
        self.send_to_guest_internal(&text.to_string())
    }

    /// Internal: send text to guest
    fn send_to_guest_internal(&self, text: &str) -> bool {
        if self.driver_fd < 0 {
            return false;
        }

        let mut clipboard = Clipboard::default();
        
        // Convert to UTF-16LE for Windows guest
        let utf16: Vec<u16> = text.encode_utf16().collect();
        let bytes: Vec<u8> = utf16.iter()
            .flat_map(|&c| c.to_le_bytes())
            .collect();

        // Check size limit
        if bytes.len() > SUNPCI_MAX_CLIPBOARD - 2 {
            tracing::warn!("Clipboard text too large: {} bytes (max {})", 
                bytes.len(), SUNPCI_MAX_CLIPBOARD - 2);
            // Truncate to fit
            let max_chars = (SUNPCI_MAX_CLIPBOARD - 2) / 2;
            let truncated: Vec<u8> = text.encode_utf16()
                .take(max_chars)
                .flat_map(|c| c.to_le_bytes())
                .collect();
            clipboard.data[..truncated.len()].copy_from_slice(&truncated);
            clipboard.length = truncated.len() as u32 + 2; // +2 for null terminator
            // Add null terminator
            clipboard.data[truncated.len()] = 0;
            clipboard.data[truncated.len() + 1] = 0;
        } else {
            clipboard.data[..bytes.len()].copy_from_slice(&bytes);
            clipboard.length = bytes.len() as u32 + 2; // +2 for null terminator
            // Add null terminator
            clipboard.data[bytes.len()] = 0;
            clipboard.data[bytes.len() + 1] = 0;
        }

        clipboard.format = clipboard_format::UNICODE;

        let result = unsafe { sunpci_set_clipboard(self.driver_fd, &clipboard) };
        
        match result {
            Ok(_) => true,
            Err(e) => {
                tracing::error!("Failed to set guest clipboard: {}", e);
                false
            }
        }
    }

    /// Get text from guest clipboard (callable from QML)
    pub fn get_from_guest(self: Pin<&mut Self>) -> QString {
        match self.get_from_guest_internal() {
            Some(text) => QString::from(&text),
            None => QString::from(""),
        }
    }

    /// Internal: get text from guest
    fn get_from_guest_internal(&self) -> Option<String> {
        if self.driver_fd < 0 {
            return None;
        }

        let mut clipboard = Clipboard::default();

        let result = unsafe { sunpci_get_clipboard(self.driver_fd, &mut clipboard) };

        match result {
            Ok(_) => {
                if clipboard.length == 0 {
                    return None;
                }

                let len = clipboard.length as usize;
                if len > SUNPCI_MAX_CLIPBOARD {
                    tracing::warn!("Invalid clipboard length from guest: {}", len);
                    return None;
                }

                let text = if clipboard.format == clipboard_format::UNICODE {
                    // UTF-16LE from Windows
                    decode_utf16le(&clipboard.data[..len])
                } else {
                    // Plain text (assume ASCII/Latin-1)
                    String::from_utf8_lossy(&clipboard.data[..len])
                        .trim_end_matches('\0')
                        .to_string()
                };

                Some(text)
            }
            Err(e) => {
                // Don't log every poll failure - EAGAIN is normal when no clipboard data
                tracing::trace!("Failed to get guest clipboard: {}", e);
                None
            }
        }
    }

    /// Get clipboard statistics
    pub fn get_stats(&self) -> QString {
        QString::from(&format!(
            "Host→Guest: {} | Guest→Host: {}",
            self.host_to_guest_count, self.guest_to_host_count
        ))
    }
}

/// Decode UTF-16LE bytes to String
fn decode_utf16le(bytes: &[u8]) -> String {
    if bytes.len() < 2 {
        return String::new();
    }

    // Convert bytes to u16 values
    let u16_values: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
        .take_while(|&c| c != 0) // Stop at null terminator
        .collect();

    String::from_utf16_lossy(&u16_values)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_decode_utf16le() {
        // "Hello" in UTF-16LE
        let bytes = [0x48, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F, 0x00, 0x00, 0x00];
        assert_eq!(decode_utf16le(&bytes), "Hello");
    }

    #[test]
    fn test_decode_utf16le_unicode() {
        // "日本" in UTF-16LE
        let bytes = [0xE5, 0x65, 0x2C, 0x67, 0x00, 0x00];
        assert_eq!(decode_utf16le(&bytes), "日本");
    }

    #[test]
    fn test_hash_text() {
        let h1 = hash_text("hello");
        let h2 = hash_text("hello");
        let h3 = hash_text("world");
        assert_eq!(h1, h2);
        assert_ne!(h1, h3);
    }
}
