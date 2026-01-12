/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * SunPCI driver userspace API
 *
 * This header is shared between the kernel driver and userspace.
 * Keep in sync with common/src/ioctl.rs
 * See docs/api-contract.md for the full specification.
 */

#ifndef _UAPI_SUNPCI_IOCTL_H
#define _UAPI_SUNPCI_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/* Magic number for SunPCI ioctls */
#define SUNPCI_IOC_MAGIC 'S'

/* Maximum path length */
#define SUNPCI_MAX_PATH 256

/* Maximum clipboard size (must fit in ioctl, max ~8KB) */
#define SUNPCI_MAX_CLIPBOARD 4096

/* Maximum drive mappings */
#define SUNPCI_MAX_DRIVE_MAPS 24

/* ============================================================================
 * ioctl Commands
 * ============================================================================ */

/* Session management */
#define SUNPCI_IOC_GET_VERSION      _IOR(SUNPCI_IOC_MAGIC, 0, struct sunpci_version)
#define SUNPCI_IOC_GET_STATUS       _IOR(SUNPCI_IOC_MAGIC, 1, struct sunpci_status)
#define SUNPCI_IOC_START_SESSION    _IOW(SUNPCI_IOC_MAGIC, 2, struct sunpci_session_config)
#define SUNPCI_IOC_STOP_SESSION     _IO(SUNPCI_IOC_MAGIC, 3)
#define SUNPCI_IOC_RESET_SESSION    _IO(SUNPCI_IOC_MAGIC, 4)

/* Display */
#define SUNPCI_IOC_GET_DISPLAY      _IOR(SUNPCI_IOC_MAGIC, 10, struct sunpci_display_info)
#define SUNPCI_IOC_SET_DISPLAY      _IOW(SUNPCI_IOC_MAGIC, 11, struct sunpci_display_config)
#define SUNPCI_IOC_GET_FRAMEBUFFER  _IOR(SUNPCI_IOC_MAGIC, 12, struct sunpci_framebuffer)

/* Storage */
#define SUNPCI_IOC_MOUNT_DISK       _IOW(SUNPCI_IOC_MAGIC, 20, struct sunpci_disk_mount)
#define SUNPCI_IOC_UNMOUNT_DISK     _IOW(SUNPCI_IOC_MAGIC, 21, struct sunpci_disk_slot)
#define SUNPCI_IOC_MOUNT_CDROM      _IOW(SUNPCI_IOC_MAGIC, 22, struct sunpci_path)
#define SUNPCI_IOC_EJECT_CDROM      _IO(SUNPCI_IOC_MAGIC, 23)
#define SUNPCI_IOC_MOUNT_FLOPPY     _IOW(SUNPCI_IOC_MAGIC, 24, struct sunpci_floppy_mount)
#define SUNPCI_IOC_EJECT_FLOPPY     _IOW(SUNPCI_IOC_MAGIC, 25, struct sunpci_floppy_slot)

/* Input */
#define SUNPCI_IOC_KEYBOARD_EVENT   _IOW(SUNPCI_IOC_MAGIC, 30, struct sunpci_key_event)
#define SUNPCI_IOC_MOUSE_EVENT      _IOW(SUNPCI_IOC_MAGIC, 31, struct sunpci_mouse_event)

/* Clipboard */
#define SUNPCI_IOC_SET_CLIPBOARD    _IOW(SUNPCI_IOC_MAGIC, 40, struct sunpci_clipboard)
#define SUNPCI_IOC_GET_CLIPBOARD    _IOR(SUNPCI_IOC_MAGIC, 41, struct sunpci_clipboard)

/* Filesystem redirection */
#define SUNPCI_IOC_ADD_DRIVE_MAP    _IOW(SUNPCI_IOC_MAGIC, 50, struct sunpci_drive_mapping)
#define SUNPCI_IOC_REMOVE_DRIVE_MAP _IOW(SUNPCI_IOC_MAGIC, 51, struct sunpci_drive_letter)

/* Network */
#define SUNPCI_IOC_SET_NETWORK      _IOW(SUNPCI_IOC_MAGIC, 60, struct sunpci_network_config)
#define SUNPCI_IOC_GET_NETWORK      _IOR(SUNPCI_IOC_MAGIC, 61, struct sunpci_network_status)

/* Audio */
#define SUNPCI_IOC_GET_AUDIO_FORMAT _IOR(SUNPCI_IOC_MAGIC, 70, struct sunpci_audio_format)
#define SUNPCI_IOC_SET_AUDIO_VOLUME _IOW(SUNPCI_IOC_MAGIC, 71, struct sunpci_audio_volume)
#define SUNPCI_IOC_GET_AUDIO_VOLUME _IOR(SUNPCI_IOC_MAGIC, 72, struct sunpci_audio_volume)
#define SUNPCI_IOC_GET_AUDIO_STATUS _IOR(SUNPCI_IOC_MAGIC, 73, struct sunpci_audio_status)
#define SUNPCI_IOC_READ_AUDIO       _IOWR(SUNPCI_IOC_MAGIC, 74, struct sunpci_audio_buffer)

/* ============================================================================
 * Session Management Structures
 * ============================================================================ */

/**
 * struct sunpci_version - Driver version information
 * @major: Major version number
 * @minor: Minor version number
 * @patch: Patch version number
 */
struct sunpci_version {
    __u32 major;
    __u32 minor;
    __u32 patch;
};

/**
 * enum sunpci_state - Session state values
 */
enum sunpci_state {
    SUNPCI_STATE_STOPPED  = 0,
    SUNPCI_STATE_STARTING = 1,
    SUNPCI_STATE_RUNNING  = 2,
    SUNPCI_STATE_STOPPING = 3,
    SUNPCI_STATE_ERROR    = 4,
};

/**
 * struct sunpci_status - Session status
 * @state: Current session state (enum sunpci_state)
 * @cpu_usage: CPU usage (percent * 100, 0-10000)
 * @memory_used: Memory used in bytes
 * @uptime_ns: Session uptime in nanoseconds
 * @disk_activity: Bitmap of active drives (bit 0=C, bit 1=D, etc.)
 * @network_rx_packets: Network packets received
 * @network_tx_packets: Network packets transmitted
 *
 * Note: Layout uses explicit u32 pairs for 64-bit values to ensure
 * consistent struct layout between 32-bit and 64-bit architectures.
 */
struct sunpci_status {
    __u32 state;
    __u32 cpu_usage;
    __u32 memory_used_lo;
    __u32 memory_used_hi;
    __u32 uptime_ns_lo;
    __u32 uptime_ns_hi;
    __u32 disk_activity;
    __u32 network_rx_packets;
    __u32 network_tx_packets;
    __u32 _pad;              /* pad to 8-byte alignment */
};

/* Configuration flags */
#define SUNPCI_FLAG_NETWORK_ENABLED    (1 << 0)
#define SUNPCI_FLAG_CLIPBOARD_ENABLED  (1 << 1)
#define SUNPCI_FLAG_CLIPBOARD_TO_HOST  (1 << 2)
#define SUNPCI_FLAG_CLIPBOARD_TO_GUEST (1 << 3)

/**
 * struct sunpci_session_config - Session configuration
 * @memory_mb: Memory size in megabytes (1-256)
 * @flags: Configuration flags (SUNPCI_FLAG_*)
 * @primary_disk: Path to primary disk image (C:)
 * @secondary_disk: Path to secondary disk image (D:)
 * @bios_path: Path to BIOS file (empty for default)
 */
struct sunpci_session_config {
    __u32 memory_mb;
    __u32 flags;
    char primary_disk[SUNPCI_MAX_PATH];
    char secondary_disk[SUNPCI_MAX_PATH];
    char bios_path[SUNPCI_MAX_PATH];
};

/* ============================================================================
 * Display Structures
 * ============================================================================ */

/**
 * struct sunpci_display_info - Display information (from guest)
 * @width: Display width in pixels
 * @height: Display height in pixels
 * @color_depth: Bits per pixel (1, 2, 4, 8, 15, 16, 24, 32)
 * @mode: Display mode (0=text, 1=graphics)
 * @text_cols: Text mode columns
 * @text_rows: Text mode rows
 */
struct sunpci_display_info {
    __u32 width;
    __u32 height;
    __u32 color_depth;
    __u32 mode;
    __u32 text_cols;
    __u32 text_rows;
};

/* Display mode values */
#define SUNPCI_DISPLAY_MODE_TEXT     0
#define SUNPCI_DISPLAY_MODE_GRAPHICS 1

/* Display configuration flags */
#define SUNPCI_DISPLAY_MAINTAIN_ASPECT (1 << 0)
#define SUNPCI_DISPLAY_SCANLINES       (1 << 1)

/**
 * struct sunpci_display_config - Display configuration (host presentation)
 * @scale_mode: Scaling mode (0=none, 1=fit, 2=integer)
 * @scale_factor: Scale factor for integer scaling
 * @flags: Display flags (SUNPCI_DISPLAY_*)
 */
struct sunpci_display_config {
    __u32 scale_mode;
    __u32 scale_factor;
    __u32 flags;
};

/* Scale mode values */
#define SUNPCI_SCALE_NONE    0
#define SUNPCI_SCALE_FIT     1
#define SUNPCI_SCALE_INTEGER 2

/* Pixel formats */
#define SUNPCI_FORMAT_INDEXED8  0  /* 8-bit indexed (palette) */
#define SUNPCI_FORMAT_RGB565    1  /* 16-bit RGB 5-6-5 */
#define SUNPCI_FORMAT_RGB888    2  /* 24-bit RGB */
#define SUNPCI_FORMAT_XRGB8888  3  /* 32-bit XRGB */

/**
 * struct sunpci_framebuffer - Framebuffer information
 * @phys_addr_lo: Physical address for mmap (low 32 bits)
 * @phys_addr_hi: Physical address for mmap (high 32 bits)
 * @size_lo: Buffer size in bytes (low 32 bits)
 * @size_hi: Buffer size in bytes (high 32 bits)
 * @stride: Bytes per row
 * @format: Pixel format (SUNPCI_FORMAT_*)
 */
struct sunpci_framebuffer {
    __u32 phys_addr_lo;
    __u32 phys_addr_hi;
    __u32 size_lo;
    __u32 size_hi;
    __u32 stride;
    __u32 format;
};

/* ============================================================================
 * Storage Structures
 * ============================================================================ */

/* Disk mount flags */
#define SUNPCI_DISK_READONLY  (1 << 0)
#define SUNPCI_DISK_CREATE    (1 << 1)

/**
 * struct sunpci_disk_mount - Disk mount request
 * @slot: Disk slot (0=primary/C:, 1=secondary/D:)
 * @flags: Mount flags (SUNPCI_DISK_*)
 * @path: Path to disk image file
 */
struct sunpci_disk_mount {
    __u32 slot;
    __u32 flags;
    char path[SUNPCI_MAX_PATH];
};

/**
 * struct sunpci_disk_slot - Disk slot identifier
 * @slot: Disk slot number
 */
struct sunpci_disk_slot {
    __u32 slot;
};

/**
 * struct sunpci_path - Generic path container
 * @path: File path
 */
struct sunpci_path {
    char path[SUNPCI_MAX_PATH];
};

/**
 * struct sunpci_floppy_mount - Floppy mount request
 * @drive: Floppy drive (0=A, 1=B)
 * @flags: Mount flags
 * @path: Path to floppy image file
 */
struct sunpci_floppy_mount {
    __u32 drive;
    __u32 flags;
    char path[SUNPCI_MAX_PATH];
};

/**
 * struct sunpci_floppy_slot - Floppy slot identifier
 * @drive: Floppy drive number
 */
struct sunpci_floppy_slot {
    __u32 drive;
};

/* ============================================================================
 * Input Structures
 * ============================================================================ */

/* Key event flags */
#define SUNPCI_KEY_PRESSED   (1 << 0)
#define SUNPCI_KEY_EXTENDED  (1 << 1)

/**
 * struct sunpci_key_event - Keyboard event
 * @scancode: XT scancode
 * @flags: Key flags (SUNPCI_KEY_*)
 */
struct sunpci_key_event {
    __u32 scancode;
    __u32 flags;
};

/* Mouse button flags */
#define SUNPCI_MOUSE_LEFT   (1 << 0)
#define SUNPCI_MOUSE_RIGHT  (1 << 1)
#define SUNPCI_MOUSE_MIDDLE (1 << 2)

/**
 * struct sunpci_mouse_event - Mouse event
 * @dx: Relative X movement
 * @dy: Relative Y movement
 * @dz: Wheel movement
 * @buttons: Button state bitmap (SUNPCI_MOUSE_*)
 */
struct sunpci_mouse_event {
    __s32 dx;
    __s32 dy;
    __s32 dz;
    __u32 buttons;
};

/* ============================================================================
 * Clipboard Structures
 * ============================================================================ */

/* Clipboard formats */
#define SUNPCI_CLIPBOARD_TEXT    0
#define SUNPCI_CLIPBOARD_UNICODE 1

/**
 * struct sunpci_clipboard - Clipboard data
 * @length: Data length in bytes
 * @format: Clipboard format (SUNPCI_CLIPBOARD_*)
 * @data: Clipboard data (up to SUNPCI_MAX_CLIPBOARD bytes)
 */
struct sunpci_clipboard {
    __u32 length;
    __u32 format;
    char data[SUNPCI_MAX_CLIPBOARD];
};

/* ============================================================================
 * Filesystem Redirection Structures
 * ============================================================================ */

/* Drive mapping flags */
#define SUNPCI_DRIVE_READONLY  (1 << 0)
#define SUNPCI_DRIVE_HIDDEN    (1 << 1)

/**
 * struct sunpci_drive_mapping - Drive mapping
 * @letter: Drive letter ('E' through 'Z')
 * @flags: Mapping flags (SUNPCI_DRIVE_*)
 * @reserved: Reserved for alignment
 * @path: Host filesystem path
 */
struct sunpci_drive_mapping {
    __u8 letter;
    __u8 flags;
    __u16 reserved;
    char path[SUNPCI_MAX_PATH];
};

/**
 * struct sunpci_drive_letter - Drive letter for unmapping
 * @letter: Drive letter
 */
struct sunpci_drive_letter {
    __u8 letter;
    __u8 _pad[3];
};

/* ============================================================================
 * Network Structures
 * ============================================================================ */

/* Network flags */
#define SUNPCI_NET_ENABLED     (1 << 0)
#define SUNPCI_NET_PROMISCUOUS (1 << 1)

/**
 * struct sunpci_network_config - Network configuration
 * @flags: Network flags (SUNPCI_NET_*)
 * @interface: Host network interface name
 * @mac_address: MAC address (6 bytes)
 * @reserved: Reserved for alignment
 */
struct sunpci_network_config {
    __u32 flags;
    char interface[32];
    __u8 mac_address[6];
    __u16 reserved;
};

/**
 * struct sunpci_network_status - Network status
 * @flags: Current network flags
 * @rx_packets: Packets received
 * @tx_packets: Packets transmitted
 * @rx_bytes: Bytes received
 * @tx_bytes: Bytes transmitted
 */
struct sunpci_network_status {
    __u32 flags;
    __u32 rx_packets;
    __u32 tx_packets;
    __u64 rx_bytes;
    __u64 tx_bytes;
};

/* ============================================================================
 * Audio Structures
 * ============================================================================ */

/* Audio format flags */
#define SUNPCI_AUDIO_FMT_16BIT   (1 << 0)    /* 16-bit samples (vs 8-bit) */
#define SUNPCI_AUDIO_FMT_STEREO  (1 << 1)    /* Stereo (vs mono) */
#define SUNPCI_AUDIO_FMT_SIGNED  (1 << 2)    /* Signed (vs unsigned) */

/* Audio status flags */
#define SUNPCI_AUDIO_PLAYING     (1 << 0)    /* Playback active */
#define SUNPCI_AUDIO_AVAILABLE   (1 << 1)    /* Audio hardware present */
#define SUNPCI_AUDIO_MUTED       (1 << 2)    /* Output muted */

/**
 * struct sunpci_audio_format - Audio format information
 * @sample_rate: Sample rate in Hz (e.g., 44100)
 * @format: Format flags (SUNPCI_AUDIO_FMT_*)
 * @channels: Number of channels (1=mono, 2=stereo)
 * @bits_per_sample: Bits per sample (8 or 16)
 */
struct sunpci_audio_format {
    __u32 sample_rate;
    __u32 format;
    __u32 channels;
    __u32 bits_per_sample;
};

/**
 * struct sunpci_audio_volume - Audio volume levels
 * @left: Left channel volume (0-255)
 * @right: Right channel volume (0-255)
 * @muted: Mute flag
 * @reserved: Reserved for alignment
 */
struct sunpci_audio_volume {
    __u8 left;
    __u8 right;
    __u8 muted;
    __u8 reserved;
};

/**
 * struct sunpci_audio_status - Audio subsystem status
 * @flags: Status flags (SUNPCI_AUDIO_*)
 * @sample_rate: Current sample rate
 * @format: Current format flags
 * @buffer_available: Bytes of audio data available
 * @samples_played: Total samples played (low 32 bits)
 * @samples_played_hi: Total samples played (high 32 bits)
 * @underruns: Buffer underrun count
 */
struct sunpci_audio_status {
    __u32 flags;
    __u32 sample_rate;
    __u32 format;
    __u32 buffer_available;
    __u32 samples_played_lo;
    __u32 samples_played_hi;
    __u32 underruns;
    __u32 reserved;
};

/* Maximum audio buffer size for single ioctl read */
#define SUNPCI_AUDIO_MAX_BUFFER  16384

/**
 * struct sunpci_audio_buffer - Audio buffer for reading samples
 * @size: On input: max bytes to read. On output: bytes actually read.
 * @data: Audio sample data
 */
struct sunpci_audio_buffer {
    __u32 size;
    __u32 reserved;
    __u8 data[SUNPCI_AUDIO_MAX_BUFFER];
};

#endif /* _UAPI_SUNPCI_IOCTL_H */
