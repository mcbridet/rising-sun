//! ioctl definitions that mirror the kernel driver interface.
//!
//! These definitions must stay in sync with driver/include/uapi/sunpci_ioctl.h
//! See docs/api-contract.md for the full specification.

use nix::ioctl_none;
use nix::ioctl_read;
use nix::ioctl_readwrite;
use nix::ioctl_write_ptr;

/// Magic number for SunPCi ioctls
pub const SUNPCI_IOC_MAGIC: u8 = b'S';

/// Maximum path length (must match kernel)
pub const SUNPCI_MAX_PATH: usize = 256;

/// Maximum clipboard size (must match kernel, must fit in ioctl ~8KB max)
pub const SUNPCI_MAX_CLIPBOARD: usize = 4096;

// ============================================================================
// ioctl Command Numbers
// ============================================================================

pub mod cmd {
    // Session management
    pub const GET_VERSION: u8 = 0;
    pub const GET_STATUS: u8 = 1;
    pub const START_SESSION: u8 = 2;
    pub const STOP_SESSION: u8 = 3;
    pub const RESET_SESSION: u8 = 4;

    // Display
    pub const GET_DISPLAY: u8 = 10;
    pub const SET_DISPLAY: u8 = 11;
    pub const GET_FRAMEBUFFER: u8 = 12;

    // Storage
    pub const MOUNT_DISK: u8 = 20;
    pub const UNMOUNT_DISK: u8 = 21;
    pub const MOUNT_CDROM: u8 = 22;
    pub const EJECT_CDROM: u8 = 23;
    pub const MOUNT_FLOPPY: u8 = 24;
    pub const EJECT_FLOPPY: u8 = 25;

    // Input
    pub const KEYBOARD_EVENT: u8 = 30;
    pub const MOUSE_EVENT: u8 = 31;

    // Clipboard
    pub const SET_CLIPBOARD: u8 = 40;
    pub const GET_CLIPBOARD: u8 = 41;

    // Filesystem redirection
    pub const ADD_DRIVE_MAP: u8 = 50;
    pub const REMOVE_DRIVE_MAP: u8 = 51;

    // Network
    pub const SET_NETWORK: u8 = 60;
    pub const GET_NETWORK: u8 = 61;

    // Audio
    pub const GET_AUDIO_FORMAT: u8 = 70;
    pub const SET_AUDIO_VOLUME: u8 = 71;
    pub const GET_AUDIO_VOLUME: u8 = 72;
    pub const GET_AUDIO_STATUS: u8 = 73;
    pub const READ_AUDIO: u8 = 74;
}

// ============================================================================
// Data Structures (must match kernel structs exactly)
// ============================================================================

/// Driver version information
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DriverVersion {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

/// Session state
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SessionState {
    #[default]
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Stopping = 3,
    Error = 4,
}

/// Session status
/// 
/// Note: Uses explicit lo/hi u32 pairs for 64-bit values to ensure
/// consistent struct layout between 32-bit and 64-bit architectures.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct SessionStatus {
    pub state: u32,
    pub cpu_usage: u32,      // percent * 100 (0-10000)
    pub memory_used_lo: u32, // bytes (low 32 bits)
    pub memory_used_hi: u32, // bytes (high 32 bits)
    pub uptime_ns_lo: u32,   // nanoseconds (low 32 bits)
    pub uptime_ns_hi: u32,   // nanoseconds (high 32 bits)
    pub disk_activity: u32,  // bitmap of active drives
    pub network_rx_packets: u32,
    pub network_tx_packets: u32,
    pub _pad: u32,           // pad to 8-byte alignment
}

impl SessionStatus {
    /// Get memory_used as u64
    pub fn memory_used(&self) -> u64 {
        ((self.memory_used_hi as u64) << 32) | (self.memory_used_lo as u64)
    }

    /// Get uptime_ns as u64
    pub fn uptime_ns(&self) -> u64 {
        ((self.uptime_ns_hi as u64) << 32) | (self.uptime_ns_lo as u64)
    }
}

/// Session configuration flags
pub mod flags {
    pub const NETWORK_ENABLED: u32 = 1 << 0;
    pub const CLIPBOARD_ENABLED: u32 = 1 << 1;
    pub const CLIPBOARD_TO_HOST: u32 = 1 << 2;
    pub const CLIPBOARD_TO_GUEST: u32 = 1 << 3;
}

/// Session configuration for starting (ioctl version)
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct IoctlSessionConfig {
    pub memory_mb: u32,
    pub flags: u32,
    pub primary_disk: [u8; SUNPCI_MAX_PATH],
    pub secondary_disk: [u8; SUNPCI_MAX_PATH],
    pub bios_path: [u8; SUNPCI_MAX_PATH],
}

impl Default for IoctlSessionConfig {
    fn default() -> Self {
        Self {
            memory_mb: 64,
            flags: 0,
            primary_disk: [0; SUNPCI_MAX_PATH],
            secondary_disk: [0; SUNPCI_MAX_PATH],
            bios_path: [0; SUNPCI_MAX_PATH],
        }
    }
}

impl IoctlSessionConfig {
    /// Set a path field from a string
    pub fn set_path(dest: &mut [u8; SUNPCI_MAX_PATH], src: &str) {
        let bytes = src.as_bytes();
        let len = bytes.len().min(SUNPCI_MAX_PATH - 1);
        dest[..len].copy_from_slice(&bytes[..len]);
        dest[len] = 0;
    }
}

/// Display information (from guest)
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DisplayInfo {
    pub width: u32,
    pub height: u32,
    pub color_depth: u32,    // 1, 2, 4, 8, 15, 16, 24, 32
    pub mode: u32,           // 0=text, 1=graphics
    pub text_cols: u32,      // for text mode
    pub text_rows: u32,      // for text mode
}

/// Display configuration flags
pub mod display_flags {
    pub const MAINTAIN_ASPECT: u32 = 1 << 0;
    pub const SCANLINES: u32 = 1 << 1;
}

/// Display configuration (host presentation)
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DisplayConfig {
    pub scale_mode: u32,     // 0=none, 1=fit, 2=integer
    pub scale_factor: u32,   // for integer scaling
    pub flags: u32,
}

/// Pixel format
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PixelFormat {
    #[default]
    Indexed8 = 0,
    Rgb565 = 1,
    Rgb888 = 2,
    Xrgb8888 = 3,
}

/// Framebuffer information
/// 
/// Note: Uses explicit lo/hi u32 pairs for 64-bit values to ensure
/// consistent struct layout between 32-bit and 64-bit architectures.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FramebufferInfo {
    pub phys_addr_lo: u32,   // physical address (low 32 bits)
    pub phys_addr_hi: u32,   // physical address (high 32 bits)
    pub size_lo: u32,        // buffer size (low 32 bits)
    pub size_hi: u32,        // buffer size (high 32 bits)
    pub stride: u32,         // bytes per row
    pub format: u32,         // PixelFormat
}

impl FramebufferInfo {
    /// Get physical address as u64
    pub fn phys_addr(&self) -> u64 {
        ((self.phys_addr_hi as u64) << 32) | (self.phys_addr_lo as u64)
    }

    /// Get size as u64
    pub fn size(&self) -> u64 {
        ((self.size_hi as u64) << 32) | (self.size_lo as u64)
    }
}

/// Disk mount flags
pub mod disk_flags {
    pub const READONLY: u32 = 1 << 0;
    pub const CREATE: u32 = 1 << 1;
}

/// Disk mount request
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct DiskMount {
    pub slot: u32,           // 0=primary, 1=secondary
    pub flags: u32,
    pub path: [u8; SUNPCI_MAX_PATH],
}

impl Default for DiskMount {
    fn default() -> Self {
        Self {
            slot: 0,
            flags: 0,
            path: [0; SUNPCI_MAX_PATH],
        }
    }
}

/// Disk slot identifier
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DiskSlot {
    pub slot: u32,
}

/// Path for CD-ROM
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Path {
    pub path: [u8; SUNPCI_MAX_PATH],
}

impl Default for Path {
    fn default() -> Self {
        Self {
            path: [0; SUNPCI_MAX_PATH],
        }
    }
}

/// Floppy mount request
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FloppyMount {
    pub drive: u32,          // 0=A, 1=B
    pub flags: u32,
    pub path: [u8; SUNPCI_MAX_PATH],
}

impl Default for FloppyMount {
    fn default() -> Self {
        Self {
            drive: 0,
            flags: 0,
            path: [0; SUNPCI_MAX_PATH],
        }
    }
}

/// Floppy slot identifier
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FloppySlot {
    pub drive: u32,
}

// ============================================================================
// SCSI Structures (for CD-ROM)
// ============================================================================

/// Maximum CDB length
pub const SCSI_CDB_MAX_LEN: usize = 16;

/// Maximum sense data length (fixed format)
pub const SCSI_SENSE_MAX_LEN: usize = 18;

/// SCSI data direction
pub mod scsi_direction {
    pub const NONE: u32 = 0;
    pub const READ: u32 = 1;
    pub const WRITE: u32 = 2;
}

/// SCSI status codes
pub mod scsi_status {
    pub const GOOD: u8 = 0x00;
    pub const CHECK_CONDITION: u8 = 0x02;
    pub const BUSY: u8 = 0x08;
}

/// SCSI command request (for CD-ROM SCSI pass-through)
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ScsiRequest {
    /// SCSI Command Descriptor Block
    pub cdb: [u8; SCSI_CDB_MAX_LEN],
    /// Actual CDB length (6, 10, 12, or 16)
    pub cdb_len: u32,
    /// Data direction (scsi_direction::*)
    pub data_direction: u32,
    /// Expected data transfer length
    pub data_len: u32,
}

impl Default for ScsiRequest {
    fn default() -> Self {
        Self {
            cdb: [0; SCSI_CDB_MAX_LEN],
            cdb_len: 0,
            data_direction: scsi_direction::NONE,
            data_len: 0,
        }
    }
}

impl ScsiRequest {
    /// Create a new SCSI request with a 6-byte CDB
    pub fn new_cdb6(cdb: [u8; 6]) -> Self {
        let mut req = Self::default();
        req.cdb[..6].copy_from_slice(&cdb);
        req.cdb_len = 6;
        req
    }

    /// Create a new SCSI request with a 10-byte CDB
    pub fn new_cdb10(cdb: [u8; 10]) -> Self {
        let mut req = Self::default();
        req.cdb[..10].copy_from_slice(&cdb);
        req.cdb_len = 10;
        req
    }

    /// Set data direction and expected length for read operations
    pub fn with_read(mut self, len: u32) -> Self {
        self.data_direction = scsi_direction::READ;
        self.data_len = len;
        self
    }
}

/// SCSI command response
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ScsiResponse {
    /// SCSI status (0x00=GOOD, 0x02=CHECK CONDITION)
    pub status: u8,
    /// Sense data length (if status != GOOD)
    pub sense_len: u8,
    /// Reserved for alignment
    pub reserved: [u8; 2],
    /// Actual data transferred
    pub data_len: u32,
    /// Sense data (if CHECK CONDITION)
    pub sense: [u8; SCSI_SENSE_MAX_LEN],
}

impl Default for ScsiResponse {
    fn default() -> Self {
        Self {
            status: scsi_status::GOOD,
            sense_len: 0,
            reserved: [0; 2],
            data_len: 0,
            sense: [0; SCSI_SENSE_MAX_LEN],
        }
    }
}

impl ScsiResponse {
    /// Check if the command completed successfully
    pub fn is_good(&self) -> bool {
        self.status == scsi_status::GOOD
    }

    /// Check if a CHECK CONDITION status was returned
    pub fn is_check_condition(&self) -> bool {
        self.status == scsi_status::CHECK_CONDITION
    }

    /// Get the sense key from sense data (if available)
    pub fn sense_key(&self) -> Option<u8> {
        if self.sense_len >= 3 {
            Some(self.sense[2] & 0x0F)
        } else {
            None
        }
    }

    /// Get the additional sense code (ASC) from sense data
    pub fn asc(&self) -> Option<u8> {
        if self.sense_len >= 13 {
            Some(self.sense[12])
        } else {
            None
        }
    }
}

/// Key event flags
pub mod key_flags {
    pub const PRESSED: u32 = 1 << 0;
    pub const EXTENDED: u32 = 1 << 1;
}

/// Keyboard event
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct KeyEvent {
    pub scancode: u32,       // XT scancode
    pub flags: u32,
}

/// Mouse button flags
pub mod mouse_buttons {
    pub const LEFT: u32 = 1 << 0;
    pub const RIGHT: u32 = 1 << 1;
    pub const MIDDLE: u32 = 1 << 2;
}

/// Mouse event
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct MouseEvent {
    pub dx: i32,             // relative X movement
    pub dy: i32,             // relative Y movement
    pub dz: i32,             // wheel movement
    pub buttons: u32,        // button state bitmap
}

/// Clipboard format
pub mod clipboard_format {
    pub const TEXT: u32 = 0;
    pub const UNICODE: u32 = 1;
}

/// Clipboard data (variable size, up to SUNPCI_MAX_CLIPBOARD)
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Clipboard {
    pub length: u32,
    pub format: u32,
    pub data: [u8; SUNPCI_MAX_CLIPBOARD],
}

impl Default for Clipboard {
    fn default() -> Self {
        Self {
            length: 0,
            format: 0,
            data: [0; SUNPCI_MAX_CLIPBOARD],
        }
    }
}

/// Drive mapping flags
pub mod drive_flags {
    pub const READONLY: u8 = 1 << 0;
    pub const HIDDEN: u8 = 1 << 1;
}

/// Drive mapping
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct DriveMapping {
    pub letter: u8,          // 'E' through 'Z'
    pub flags: u8,
    pub reserved: u16,
    pub path: [u8; SUNPCI_MAX_PATH],
}

impl Default for DriveMapping {
    fn default() -> Self {
        Self {
            letter: 0,
            flags: 0,
            reserved: 0,
            path: [0; SUNPCI_MAX_PATH],
        }
    }
}

/// Drive letter for unmapping
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DriveLetter {
    pub letter: u8,
    pub _pad: [u8; 3],
}

/// Network flags
pub mod net_flags {
    pub const ENABLED: u32 = 1 << 0;
    pub const PROMISCUOUS: u32 = 1 << 1;
}

/// Network configuration
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct NetworkConfig {
    pub flags: u32,
    pub interface: [u8; 32], // host interface name
    pub mac_address: [u8; 6],
    pub reserved: u16,
}

/// Network status
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct NetworkStatus {
    pub flags: u32,
    pub rx_packets: u32,
    pub tx_packets: u32,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
}

// ============================================================================
// Audio Structures
// ============================================================================

/// Maximum audio buffer size for single ioctl read
pub const SUNPCI_AUDIO_MAX_BUFFER: usize = 16384;

/// Audio format flags
pub mod audio_format {
    pub const FMT_16BIT: u32 = 1 << 0;   // 16-bit samples (vs 8-bit)
    pub const FMT_STEREO: u32 = 1 << 1;  // Stereo (vs mono)
    pub const FMT_SIGNED: u32 = 1 << 2;  // Signed (vs unsigned)
}

/// Audio status flags
pub mod audio_status_flags {
    pub const PLAYING: u32 = 1 << 0;     // Playback active
    pub const AVAILABLE: u32 = 1 << 1;   // Audio hardware present
    pub const MUTED: u32 = 1 << 2;       // Output muted
}

/// Audio format information
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct AudioFormat {
    pub sample_rate: u32,        // Sample rate in Hz (e.g., 44100)
    pub format: u32,             // Format flags (audio_format::*)
    pub channels: u32,           // Number of channels (1=mono, 2=stereo)
    pub bits_per_sample: u32,    // Bits per sample (8 or 16)
}

impl AudioFormat {
    /// Get bytes per sample (including all channels)
    pub fn bytes_per_sample(&self) -> u32 {
        (self.bits_per_sample / 8) * self.channels
    }

    /// Get bytes per second
    pub fn bytes_per_second(&self) -> u32 {
        self.sample_rate * self.bytes_per_sample()
    }
}

/// Audio volume levels
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct AudioVolume {
    pub left: u8,                // Left channel volume (0-255)
    pub right: u8,               // Right channel volume (0-255)
    pub muted: u8,               // Mute flag
    pub reserved: u8,            // Reserved for alignment
}

impl AudioVolume {
    /// Create new volume with both channels set
    pub fn new(volume: u8) -> Self {
        Self {
            left: volume,
            right: volume,
            muted: 0,
            reserved: 0,
        }
    }

    /// Create muted volume
    pub fn muted() -> Self {
        Self {
            left: 0,
            right: 0,
            muted: 1,
            reserved: 0,
        }
    }
}

/// Audio subsystem status
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct AudioStatus {
    pub flags: u32,              // Status flags (audio_status_flags::*)
    pub sample_rate: u32,        // Current sample rate
    pub format: u32,             // Current format flags
    pub buffer_available: u32,   // Bytes of audio data available
    pub samples_played_lo: u32,  // Total samples played (low 32 bits)
    pub samples_played_hi: u32,  // Total samples played (high 32 bits)
    pub underruns: u32,          // Buffer underrun count
    pub reserved: u32,           // Reserved for alignment
}

impl AudioStatus {
    /// Get samples_played as u64
    pub fn samples_played(&self) -> u64 {
        ((self.samples_played_hi as u64) << 32) | (self.samples_played_lo as u64)
    }

    /// Check if audio is playing
    pub fn is_playing(&self) -> bool {
        self.flags & audio_status_flags::PLAYING != 0
    }

    /// Check if audio hardware is available
    pub fn is_available(&self) -> bool {
        self.flags & audio_status_flags::AVAILABLE != 0
    }

    /// Check if audio is muted
    pub fn is_muted(&self) -> bool {
        self.flags & audio_status_flags::MUTED != 0
    }
}

/// Audio buffer for reading samples
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct AudioBuffer {
    pub size: u32,               // On input: max bytes. On output: bytes read.
    pub reserved: u32,
    pub data: [u8; SUNPCI_AUDIO_MAX_BUFFER],
}

impl Default for AudioBuffer {
    fn default() -> Self {
        Self {
            size: SUNPCI_AUDIO_MAX_BUFFER as u32,
            reserved: 0,
            data: [0; SUNPCI_AUDIO_MAX_BUFFER],
        }
    }
}

// ============================================================================
// ioctl Function Wrappers
// ============================================================================

// Session management
ioctl_read!(sunpci_get_version, SUNPCI_IOC_MAGIC, cmd::GET_VERSION, DriverVersion);
ioctl_read!(sunpci_get_status, SUNPCI_IOC_MAGIC, cmd::GET_STATUS, SessionStatus);
ioctl_write_ptr!(sunpci_start_session, SUNPCI_IOC_MAGIC, cmd::START_SESSION, IoctlSessionConfig);
ioctl_none!(sunpci_stop_session, SUNPCI_IOC_MAGIC, cmd::STOP_SESSION);
ioctl_none!(sunpci_reset_session, SUNPCI_IOC_MAGIC, cmd::RESET_SESSION);

// Display
ioctl_read!(sunpci_get_display, SUNPCI_IOC_MAGIC, cmd::GET_DISPLAY, DisplayInfo);
ioctl_write_ptr!(sunpci_set_display, SUNPCI_IOC_MAGIC, cmd::SET_DISPLAY, DisplayConfig);
ioctl_read!(sunpci_get_framebuffer, SUNPCI_IOC_MAGIC, cmd::GET_FRAMEBUFFER, FramebufferInfo);

// Storage
ioctl_write_ptr!(sunpci_mount_disk, SUNPCI_IOC_MAGIC, cmd::MOUNT_DISK, DiskMount);
ioctl_write_ptr!(sunpci_unmount_disk, SUNPCI_IOC_MAGIC, cmd::UNMOUNT_DISK, DiskSlot);
ioctl_write_ptr!(sunpci_mount_cdrom, SUNPCI_IOC_MAGIC, cmd::MOUNT_CDROM, Path);
ioctl_none!(sunpci_eject_cdrom, SUNPCI_IOC_MAGIC, cmd::EJECT_CDROM);
ioctl_write_ptr!(sunpci_mount_floppy, SUNPCI_IOC_MAGIC, cmd::MOUNT_FLOPPY, FloppyMount);
ioctl_write_ptr!(sunpci_eject_floppy, SUNPCI_IOC_MAGIC, cmd::EJECT_FLOPPY, FloppySlot);

// Input
ioctl_write_ptr!(sunpci_keyboard_event, SUNPCI_IOC_MAGIC, cmd::KEYBOARD_EVENT, KeyEvent);
ioctl_write_ptr!(sunpci_mouse_event, SUNPCI_IOC_MAGIC, cmd::MOUSE_EVENT, MouseEvent);

// Clipboard
ioctl_write_ptr!(sunpci_set_clipboard, SUNPCI_IOC_MAGIC, cmd::SET_CLIPBOARD, Clipboard);
ioctl_read!(sunpci_get_clipboard, SUNPCI_IOC_MAGIC, cmd::GET_CLIPBOARD, Clipboard);

// Filesystem redirection
ioctl_write_ptr!(sunpci_add_drive_map, SUNPCI_IOC_MAGIC, cmd::ADD_DRIVE_MAP, DriveMapping);
ioctl_write_ptr!(sunpci_remove_drive_map, SUNPCI_IOC_MAGIC, cmd::REMOVE_DRIVE_MAP, DriveLetter);

// Network
ioctl_write_ptr!(sunpci_set_network, SUNPCI_IOC_MAGIC, cmd::SET_NETWORK, NetworkConfig);
ioctl_read!(sunpci_get_network, SUNPCI_IOC_MAGIC, cmd::GET_NETWORK, NetworkStatus);

// Audio
ioctl_read!(sunpci_get_audio_format, SUNPCI_IOC_MAGIC, cmd::GET_AUDIO_FORMAT, AudioFormat);
ioctl_write_ptr!(sunpci_set_audio_volume, SUNPCI_IOC_MAGIC, cmd::SET_AUDIO_VOLUME, AudioVolume);
ioctl_read!(sunpci_get_audio_volume, SUNPCI_IOC_MAGIC, cmd::GET_AUDIO_VOLUME, AudioVolume);
ioctl_read!(sunpci_get_audio_status, SUNPCI_IOC_MAGIC, cmd::GET_AUDIO_STATUS, AudioStatus);
ioctl_readwrite!(sunpci_read_audio, SUNPCI_IOC_MAGIC, cmd::READ_AUDIO, AudioBuffer);

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem;

    #[test]
    fn test_struct_sizes() {
        // Ensure structs have predictable sizes for FFI
        assert_eq!(mem::size_of::<DriverVersion>(), 12);
        assert_eq!(mem::size_of::<SessionStatus>(), 32);
        assert_eq!(mem::size_of::<DisplayInfo>(), 24);
        assert_eq!(mem::size_of::<KeyEvent>(), 8);
        assert_eq!(mem::size_of::<MouseEvent>(), 16);
    }

    #[test]
    fn test_session_config_set_path() {
        let mut config = SessionConfig::default();
        SessionConfig::set_path(&mut config.primary_disk, "/path/to/disk.img");
        assert_eq!(&config.primary_disk[..18], b"/path/to/disk.img\0");
    }
}
