//! Interface to the SunPCi kernel driver.
//!
//! This module provides a safe Rust interface to the kernel driver's ioctl commands.
//! The frontend uses this directly - no daemon required.

use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;

use anyhow::{Context, Result};

use crate::ioctl::{
    AudioBuffer, AudioFormat, AudioStatus, AudioVolume,
    Clipboard, DisplayConfig, DisplayInfo, DiskMount, DiskSlot, DriveLetter, DriveMapping,
    FloppyMount, FloppySlot, FramebufferInfo, IoctlSessionConfig, KeyEvent, MouseEvent,
    NetworkConfig, NetworkStatus, Path, SessionStatus, DriverVersion,
    SUNPCI_MAX_PATH, clipboard_format, disk_flags, drive_flags,
    sunpci_add_drive_map, sunpci_eject_cdrom, sunpci_eject_floppy, sunpci_get_clipboard,
    sunpci_get_display, sunpci_get_framebuffer, sunpci_get_network, sunpci_get_status,
    sunpci_get_version, sunpci_keyboard_event, sunpci_mount_cdrom, sunpci_mount_disk,
    sunpci_mount_floppy, sunpci_mouse_event, sunpci_remove_drive_map, sunpci_reset_session,
    sunpci_set_clipboard, sunpci_set_display, sunpci_set_network, sunpci_start_session,
    sunpci_stop_session, sunpci_unmount_disk,
    sunpci_get_audio_format, sunpci_get_audio_status, sunpci_get_audio_volume,
    sunpci_set_audio_volume, sunpci_read_audio,
};
use crate::SunPciError;

const DEVICE_PATH: &str = "/dev/sunpci0";

/// Check if the SunPCi driver is loaded
pub fn is_driver_loaded() -> bool {
    std::path::Path::new(DEVICE_PATH).exists()
}

/// Handle to the SunPCi device.
/// 
/// This provides direct access to the kernel driver via ioctl.
/// Only one instance should be active at a time since the hardware
/// is single-user (one display, one keyboard/mouse, one set of drives).
pub struct DriverHandle {
    file: File,
}

impl DriverHandle {
    /// Open the SunPCi device.
    /// 
    /// Requires read/write access to /dev/sunpci0.
    /// Use udev rules to grant access to a 'sunpci' group.
    pub fn open() -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(DEVICE_PATH)
            .with_context(|| format!("Failed to open {}", DEVICE_PATH))?;

        Ok(Self { file })
    }

    /// Get the raw file descriptor (for mmap, polling, etc.)
    pub fn as_raw_fd(&self) -> i32 {
        self.file.as_raw_fd()
    }

    // ========================================================================
    // Session Management
    // ========================================================================

    /// Get the driver version
    pub fn get_version(&self) -> Result<DriverVersion> {
        let mut version = DriverVersion::default();
        unsafe {
            sunpci_get_version(self.file.as_raw_fd(), &mut version)
                .map_err(SunPciError::from)?;
        }
        Ok(version)
    }

    /// Get the current session status
    pub fn get_status(&self) -> Result<SessionStatus> {
        let mut status = SessionStatus::default();
        unsafe {
            sunpci_get_status(self.file.as_raw_fd(), &mut status)
                .map_err(SunPciError::from)?;
        }
        Ok(status)
    }

    /// Start session with configuration
    pub fn start_session(&self, config: &IoctlSessionConfig) -> Result<()> {
        unsafe {
            sunpci_start_session(self.file.as_raw_fd(), config)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Stop session
    pub fn stop_session(&self) -> Result<()> {
        unsafe {
            sunpci_stop_session(self.file.as_raw_fd())
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Reset session (warm reboot / Ctrl+Alt+Del)
    pub fn reset_session(&self) -> Result<()> {
        unsafe {
            sunpci_reset_session(self.file.as_raw_fd())
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    // ========================================================================
    // Display
    // ========================================================================

    /// Get current display information from guest
    pub fn get_display(&self) -> Result<DisplayInfo> {
        let mut info = DisplayInfo::default();
        unsafe {
            sunpci_get_display(self.file.as_raw_fd(), &mut info)
                .map_err(SunPciError::from)?;
        }
        Ok(info)
    }

    /// Set display configuration (scaling, etc.)
    pub fn set_display(&self, config: &DisplayConfig) -> Result<()> {
        unsafe {
            sunpci_set_display(self.file.as_raw_fd(), config)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Get framebuffer information for rendering
    pub fn get_framebuffer(&self) -> Result<FramebufferInfo> {
        let mut info = FramebufferInfo::default();
        unsafe {
            sunpci_get_framebuffer(self.file.as_raw_fd(), &mut info)
                .map_err(SunPciError::from)?;
        }
        Ok(info)
    }

    // ========================================================================
    // Storage
    // ========================================================================

    /// Mount a disk image (slot 0 = C:, slot 1 = D:)
    pub fn mount_disk(&self, slot: u32, path: &str, readonly: bool) -> Result<()> {
        let mut mount = DiskMount::default();
        mount.slot = slot;
        mount.flags = if readonly { disk_flags::READONLY } else { 0 };
        set_path(&mut mount.path, path);
        unsafe {
            sunpci_mount_disk(self.file.as_raw_fd(), &mount)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Unmount a disk
    pub fn unmount_disk(&self, slot: u32) -> Result<()> {
        let disk_slot = DiskSlot { slot };
        unsafe {
            sunpci_unmount_disk(self.file.as_raw_fd(), &disk_slot)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Mount a CD-ROM ISO image
    pub fn mount_cdrom(&self, path: &str) -> Result<()> {
        let mut p = Path::default();
        set_path(&mut p.path, path);
        unsafe {
            sunpci_mount_cdrom(self.file.as_raw_fd(), &p)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Eject CD-ROM
    pub fn eject_cdrom(&self) -> Result<()> {
        unsafe {
            sunpci_eject_cdrom(self.file.as_raw_fd())
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Mount a floppy image (drive 0 = A:, drive 1 = B:)
    pub fn mount_floppy(&self, drive: u32, path: &str) -> Result<()> {
        let mut mount = FloppyMount::default();
        mount.drive = drive;
        set_path(&mut mount.path, path);
        unsafe {
            sunpci_mount_floppy(self.file.as_raw_fd(), &mount)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Eject floppy
    pub fn eject_floppy(&self, drive: u32) -> Result<()> {
        let slot = FloppySlot { drive };
        unsafe {
            sunpci_eject_floppy(self.file.as_raw_fd(), &slot)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    // ========================================================================
    // Input
    // ========================================================================

    /// Send a keyboard event to the guest
    pub fn send_key_event(&self, event: &KeyEvent) -> Result<()> {
        unsafe {
            sunpci_keyboard_event(self.file.as_raw_fd(), event)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Send a mouse event to the guest
    pub fn send_mouse_event(&self, event: &MouseEvent) -> Result<()> {
        unsafe {
            sunpci_mouse_event(self.file.as_raw_fd(), event)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    // ========================================================================
    // Clipboard
    // ========================================================================

    /// Set clipboard content (host to guest)
    pub fn set_clipboard(&self, text: &str) -> Result<()> {
        let mut clipboard = Clipboard::default();
        let bytes = text.as_bytes();
        let len = bytes.len().min(clipboard.data.len() - 1);
        clipboard.data[..len].copy_from_slice(&bytes[..len]);
        clipboard.length = len as u32;
        clipboard.format = clipboard_format::TEXT;
        unsafe {
            sunpci_set_clipboard(self.file.as_raw_fd(), &clipboard)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Get clipboard content (from guest)
    pub fn get_clipboard(&self) -> Result<String> {
        let mut clipboard = Clipboard::default();
        unsafe {
            sunpci_get_clipboard(self.file.as_raw_fd(), &mut clipboard)
                .map_err(SunPciError::from)?;
        }
        let len = clipboard.length as usize;
        let text = std::str::from_utf8(&clipboard.data[..len])
            .context("Invalid UTF-8 in clipboard")?
            .to_string();
        Ok(text)
    }

    // ========================================================================
    // Drive Mappings (filesystem redirection)
    // ========================================================================

    /// Add a drive mapping (E: through Z: mapped to host paths)
    pub fn add_drive_mapping(&self, letter: char, path: &str, readonly: bool) -> Result<()> {
        let mut mapping = DriveMapping::default();
        mapping.letter = letter as u8;
        mapping.flags = if readonly { drive_flags::READONLY } else { 0 };
        set_path(&mut mapping.path, path);
        unsafe {
            sunpci_add_drive_map(self.file.as_raw_fd(), &mapping)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Remove a drive mapping
    pub fn remove_drive_mapping(&self, letter: char) -> Result<()> {
        let drive_letter = DriveLetter { letter: letter as u8, _pad: [0; 3] };
        unsafe {
            sunpci_remove_drive_map(self.file.as_raw_fd(), &drive_letter)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    // ========================================================================
    // Network
    // ========================================================================

    /// Configure network adapter
    pub fn set_network(&self, config: &NetworkConfig) -> Result<()> {
        unsafe {
            sunpci_set_network(self.file.as_raw_fd(), config)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Get network status
    pub fn get_network(&self) -> Result<NetworkStatus> {
        let mut status = NetworkStatus::default();
        unsafe {
            sunpci_get_network(self.file.as_raw_fd(), &mut status)
                .map_err(SunPciError::from)?;
        }
        Ok(status)
    }

    // ========================================================================
    // Audio
    // ========================================================================

    /// Get current audio format (sample rate, channels, bit depth)
    pub fn get_audio_format(&self) -> Result<AudioFormat> {
        let mut format = AudioFormat::default();
        unsafe {
            sunpci_get_audio_format(self.file.as_raw_fd(), &mut format)
                .map_err(SunPciError::from)?;
        }
        Ok(format)
    }

    /// Get audio subsystem status
    pub fn get_audio_status(&self) -> Result<AudioStatus> {
        let mut status = AudioStatus::default();
        unsafe {
            sunpci_get_audio_status(self.file.as_raw_fd(), &mut status)
                .map_err(SunPciError::from)?;
        }
        Ok(status)
    }

    /// Get current volume levels
    pub fn get_audio_volume(&self) -> Result<AudioVolume> {
        let mut volume = AudioVolume::default();
        unsafe {
            sunpci_get_audio_volume(self.file.as_raw_fd(), &mut volume)
                .map_err(SunPciError::from)?;
        }
        Ok(volume)
    }

    /// Set volume levels
    pub fn set_audio_volume(&self, left: u8, right: u8, muted: bool) -> Result<()> {
        let volume = AudioVolume {
            left,
            right,
            muted: if muted { 1 } else { 0 },
            reserved: 0,
        };
        unsafe {
            sunpci_set_audio_volume(self.file.as_raw_fd(), &volume)
                .map_err(SunPciError::from)?;
        }
        Ok(())
    }

    /// Read audio samples from the driver
    /// Returns the number of bytes read and the data
    pub fn read_audio(&self, max_bytes: usize) -> Result<Vec<u8>> {
        let mut buffer = AudioBuffer::default();
        buffer.size = max_bytes.min(buffer.data.len()) as u32;
        
        unsafe {
            sunpci_read_audio(self.file.as_raw_fd(), &mut buffer)
                .map_err(SunPciError::from)?;
        }
        
        let bytes_read = buffer.size as usize;
        Ok(buffer.data[..bytes_read].to_vec())
    }

    /// Check if audio hardware is available
    pub fn is_audio_available(&self) -> bool {
        self.get_audio_status()
            .map(|s| s.is_available())
            .unwrap_or(false)
    }
}

/// Helper to set a path in a fixed-size buffer
fn set_path(dest: &mut [u8; SUNPCI_MAX_PATH], src: &str) {
    let bytes = src.as_bytes();
    let len = bytes.len().min(SUNPCI_MAX_PATH - 1);
    dest[..len].copy_from_slice(&bytes[..len]);
    dest[len] = 0;
}
