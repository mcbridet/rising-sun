//! Disk manager Qt bridge for handling virtual disk operations.

use std::fs::File;
use std::io::{Read, Write, Seek, SeekFrom};
use std::path::Path;

use rising_sun_common::{DriverHandle, is_driver_loaded};

#[cxx_qt::bridge]
mod qobject {
    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(QString, primary_disk_path)]
        #[qproperty(QString, secondary_disk_path)]
        #[qproperty(QString, floppy_a_path)]
        #[qproperty(QString, floppy_b_path)]
        #[qproperty(QString, cdrom_path)]
        #[qproperty(bool, primary_mounted)]
        #[qproperty(bool, secondary_mounted)]
        #[qproperty(bool, floppy_a_mounted)]
        #[qproperty(bool, floppy_b_mounted)]
        #[qproperty(bool, cdrom_mounted)]
        type DiskManager = super::DiskManagerRust;

        /// Create a new disk image
        #[qinvokable]
        fn create_disk(self: &DiskManager, path: QString, size_mb: i32, revision: i32) -> bool;

        /// Mount a disk image to primary (slot 0) or secondary (slot 1)
        #[qinvokable]
        fn mount_disk(self: Pin<&mut DiskManager>, path: QString, slot: i32) -> bool;

        /// Unmount a disk from a slot
        #[qinvokable]
        fn unmount_disk(self: Pin<&mut DiskManager>, slot: i32) -> bool;

        /// Mount a floppy image
        #[qinvokable]
        fn mount_floppy(self: Pin<&mut DiskManager>, path: QString, drive_number: i32) -> bool;

        /// Eject a floppy
        #[qinvokable]
        fn eject_floppy(self: Pin<&mut DiskManager>, drive_number: i32);

        /// Mount an ISO image
        #[qinvokable]
        fn mount_iso(self: Pin<&mut DiskManager>, path: QString) -> bool;

        /// Eject the CD-ROM
        #[qinvokable]
        fn eject_cdrom(self: Pin<&mut DiskManager>);

        /// Get disk information as JSON string
        #[qinvokable]
        fn get_disk_info(self: &DiskManager, path: QString) -> QString;

        /// Check if the disk at path is a valid SunPCi disk image
        #[qinvokable]
        fn is_valid_disk(self: &DiskManager, path: QString) -> bool;

        /// Get the size of a disk image in MB
        #[qinvokable]
        fn get_disk_size_mb(self: &DiskManager, path: QString) -> i32;
    }

    unsafe extern "C++Qt" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }
}

use std::pin::Pin;
use cxx_qt_lib::QString;

/// Rust implementation of the DiskManager
pub struct DiskManagerRust {
    primary_disk_path: QString,
    secondary_disk_path: QString,
    floppy_a_path: QString,
    floppy_b_path: QString,
    cdrom_path: QString,
    primary_mounted: bool,
    secondary_mounted: bool,
    floppy_a_mounted: bool,
    floppy_b_mounted: bool,
    cdrom_mounted: bool,
}

impl Default for DiskManagerRust {
    fn default() -> Self {
        Self {
            primary_disk_path: QString::default(),
            secondary_disk_path: QString::default(),
            floppy_a_path: QString::default(),
            floppy_b_path: QString::default(),
            cdrom_path: QString::default(),
            primary_mounted: false,
            secondary_mounted: false,
            floppy_a_mounted: false,
            floppy_b_mounted: false,
            cdrom_mounted: false,
        }
    }
}

impl qobject::DiskManager {
    /// Create a new disk image
    /// 
    /// Creates a SunPCi-compatible disk image with:
    /// - Magic "SPCI" at offset 12
    /// - MBR partition table
    /// - FAT16 filesystem (for sizes > 32MB) or FAT12 (smaller)
    pub fn create_disk(&self, path: QString, size_mb: i32, revision: i32) -> bool {
        let path_str = path.to_string();
        tracing::info!(
            "Creating disk: path={}, size={}MB, revision={}",
            path_str,
            size_mb,
            revision
        );

        match create_disk_image(&path_str, size_mb as u32, revision as u8) {
            Ok(()) => {
                tracing::info!("Disk created successfully: {}", path_str);
                true
            }
            Err(e) => {
                tracing::error!("Failed to create disk: {}", e);
                false
            }
        }
    }

    /// Mount a disk image to a slot (0 = primary/C:, 1 = secondary/D:)
    pub fn mount_disk(mut self: Pin<&mut Self>, path: QString, slot: i32) -> bool {
        let path_str = path.to_string();
        let drive = if slot == 0 { "C:" } else { "D:" };
        tracing::info!("Mounting disk: path={} as {} (slot {})", path_str, drive, slot);

        // Validate the disk first
        if !self.is_valid_disk(path.clone()) {
            tracing::error!("Invalid disk image: {}", path_str);
            return false;
        }

        // Expand path
        let expanded_path = expand_path(&path_str);
        let expanded_str = expanded_path.to_string_lossy().to_string();

        // Try to mount via driver - do this in separate scope to avoid borrow issues
        let mount_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.mount_disk(slot as u32, &expanded_str, false)
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };
        
        match mount_result {
            Ok(()) => {
                tracing::info!("Disk mounted successfully: {} as {}", path_str, drive);
                
                // Update properties based on slot
                if slot == 0 {
                    self.as_mut().set_primary_disk_path(path.clone());
                    self.as_mut().set_primary_mounted(true);
                } else {
                    self.as_mut().set_secondary_disk_path(path.clone());
                    self.as_mut().set_secondary_mounted(true);
                }
                true
            }
            Err(e) => {
                tracing::error!("Failed to mount disk: {}", e);
                false
            }
        }
    }

    /// Unmount a disk from a slot
    pub fn unmount_disk(mut self: Pin<&mut Self>, slot: i32) -> bool {
        let drive = if slot == 0 { "C:" } else { "D:" };
        tracing::info!("Unmounting disk from {} (slot {})", drive, slot);

        // Try to unmount via driver - separate scope to avoid borrow issues
        let unmount_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.unmount_disk(slot as u32)
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };
        
        match unmount_result {
            Ok(()) => {
                tracing::info!("Disk unmounted successfully from {}", drive);
                
                if slot == 0 {
                    self.as_mut().set_primary_disk_path(QString::default());
                    self.as_mut().set_primary_mounted(false);
                } else {
                    self.as_mut().set_secondary_disk_path(QString::default());
                    self.as_mut().set_secondary_mounted(false);
                }
                true
            }
            Err(e) => {
                tracing::error!("Failed to unmount disk: {}", e);
                false
            }
        }
    }

    /// Mount a floppy image (drive_number 0 = A:, 1 = B:)
    pub fn mount_floppy(mut self: Pin<&mut Self>, path: QString, drive_number: i32) -> bool {
        let path_str = path.to_string();
        let drive = if drive_number == 0 { "A:" } else { "B:" };
        tracing::info!("Mounting floppy: path={} as {}", path_str, drive);

        // Expand path
        let expanded_path = expand_path(&path_str);
        let expanded_str = expanded_path.to_string_lossy().to_string();

        // Check file exists and has reasonable size for floppy
        match std::fs::metadata(&expanded_path) {
            Ok(meta) => {
                let size = meta.len();
                // Floppy sizes: 360K, 720K, 1.2M, 1.44M, 2.88M
                if size > 3 * 1024 * 1024 {
                    tracing::error!("File too large for floppy image: {} bytes", size);
                    return false;
                }
            }
            Err(e) => {
                tracing::error!("Cannot access floppy image {}: {}", path_str, e);
                return false;
            }
        }

        // Mount via driver
        let mount_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.mount_floppy(drive_number as u32, &expanded_str)
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };

        match mount_result {
            Ok(()) => {
                tracing::info!("Floppy mounted successfully: {} as {}", path_str, drive);
                
                if drive_number == 0 {
                    self.as_mut().set_floppy_a_path(path.clone());
                    self.as_mut().set_floppy_a_mounted(true);
                } else {
                    self.as_mut().set_floppy_b_path(path.clone());
                    self.as_mut().set_floppy_b_mounted(true);
                }
                true
            }
            Err(e) => {
                tracing::error!("Failed to mount floppy: {}", e);
                false
            }
        }
    }

    /// Eject a floppy from drive (0 = A:, 1 = B:)
    pub fn eject_floppy(mut self: Pin<&mut Self>, drive_number: i32) {
        let drive = if drive_number == 0 { "A:" } else { "B:" };
        tracing::info!("Ejecting floppy from {}", drive);

        let eject_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.eject_floppy(drive_number as u32)
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };

        match eject_result {
            Ok(()) => {
                tracing::info!("Floppy ejected from {}", drive);
                
                if drive_number == 0 {
                    self.as_mut().set_floppy_a_path(QString::default());
                    self.as_mut().set_floppy_a_mounted(false);
                } else {
                    self.as_mut().set_floppy_b_path(QString::default());
                    self.as_mut().set_floppy_b_mounted(false);
                }
            }
            Err(e) => {
                tracing::error!("Failed to eject floppy: {}", e);
            }
        }
    }

    /// Mount an ISO image as CD-ROM
    pub fn mount_iso(mut self: Pin<&mut Self>, path: QString) -> bool {
        let path_str = path.to_string();
        tracing::info!("Mounting ISO: {}", path_str);

        // Expand path
        let expanded_path = expand_path(&path_str);
        let expanded_str = expanded_path.to_string_lossy().to_string();

        // Check file exists
        if !expanded_path.exists() {
            tracing::error!("ISO file does not exist: {}", path_str);
            return false;
        }

        // Mount via driver
        let mount_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.mount_cdrom(&expanded_str)
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };

        match mount_result {
            Ok(()) => {
                tracing::info!("ISO mounted successfully: {}", path_str);
                self.as_mut().set_cdrom_path(path.clone());
                self.as_mut().set_cdrom_mounted(true);
                true
            }
            Err(e) => {
                tracing::error!("Failed to mount ISO: {}", e);
                false
            }
        }
    }

    /// Eject the CD-ROM
    pub fn eject_cdrom(mut self: Pin<&mut Self>) {
        tracing::info!("Ejecting CD-ROM");

        let eject_result = {
            if !is_driver_loaded() {
                Err("Driver not loaded".to_string())
            } else {
                match DriverHandle::open() {
                    Ok(handle) => {
                        handle.eject_cdrom()
                            .map_err(|e| e.to_string())
                    }
                    Err(e) => Err(e.to_string()),
                }
            }
        };

        match eject_result {
            Ok(()) => {
                tracing::info!("CD-ROM ejected");
                self.as_mut().set_cdrom_path(QString::default());
                self.as_mut().set_cdrom_mounted(false);
            }
            Err(e) => {
                tracing::error!("Failed to eject CD-ROM: {}", e);
            }
        }
    }

    /// Get disk information as JSON
    /// 
    /// Returns JSON with fields:
    /// - valid: bool - whether this is a valid SunPCi disk
    /// - size_mb: number - size in megabytes
    /// - revision: number - SunPCi format revision
    /// - cylinders: number - CHS cylinders
    /// - heads: number - CHS heads
    /// - sectors: number - CHS sectors per track
    /// - total_sectors: number - total sector count
    /// - bootable: bool - whether partition is marked bootable
    /// - partition_type: string - partition type description
    pub fn get_disk_info(&self, path: QString) -> QString {
        let path_str = path.to_string();
        tracing::debug!("Getting disk info for: {}", path_str);
        
        match read_disk_header(&path_str) {
            Ok(info) => {
                QString::from(&format!(
                    r#"{{"valid": true, "size_mb": {}, "revision": {}, "cylinders": {}, "heads": {}, "sectors": {}, "total_sectors": {}, "bootable": {}, "partition_type": "{}"}}"#,
                    info.size_mb,
                    info.revision,
                    info.cylinders,
                    info.heads,
                    info.sectors_per_track,
                    info.total_sectors,
                    info.bootable,
                    info.partition_type
                ))
            }
            Err(e) => {
                tracing::warn!("Failed to read disk info for {}: {}", path_str, e);
                QString::from(r#"{"valid": false, "error": "Failed to read disk header"}"#)
            }
        }
    }

    /// Check if the disk at path is a valid SunPCi disk image
    pub fn is_valid_disk(&self, path: QString) -> bool {
        let path_str = path.to_string();
        match read_disk_header(&path_str) {
            Ok(info) => info.is_sunpci,
            Err(_) => false,
        }
    }

    /// Get the size of a disk image in MB
    pub fn get_disk_size_mb(&self, path: QString) -> i32 {
        let path_str = path.to_string();
        match read_disk_header(&path_str) {
            Ok(info) => info.size_mb as i32,
            Err(_) => 0,
        }
    }
}

/// SunPCi disk magic number: "SPCI" = 0x53504349
const SUNPCI_MAGIC: u32 = 0x53504349;

/// Sector size in bytes
const SECTOR_SIZE: u32 = 512;

/// Calculate disk geometry for a given size
/// Returns (cylinders, heads, sectors_per_track)
fn calculate_geometry(size_mb: u32) -> (u16, u8, u8) {
    let total_sectors = (size_mb as u64 * 1024 * 1024) / SECTOR_SIZE as u64;
    
    // Standard sectors per track
    let sectors_per_track: u8 = 63;
    
    // Choose heads based on disk size to stay within 1024 cylinder limit
    let heads: u8 = if size_mb <= 504 {
        16
    } else if size_mb <= 1008 {
        32
    } else if size_mb <= 2016 {
        64
    } else if size_mb <= 4032 {
        128
    } else {
        255
    };
    
    let cylinders = (total_sectors / (heads as u64 * sectors_per_track as u64)) as u16;
    let cylinders = cylinders.min(1024); // CHS limit
    
    (cylinders, heads, sectors_per_track)
}

/// Create a SunPCi-compatible disk image
fn create_disk_image(path: &str, size_mb: u32, revision: u8) -> std::io::Result<()> {
    // Expand ~ to home directory
    let expanded_path = if path.starts_with("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            Path::new(&home).join(&path[2..])
        } else {
            Path::new(path).to_path_buf()
        }
    } else {
        Path::new(path).to_path_buf()
    };
    
    // Create parent directories if needed
    if let Some(parent) = expanded_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    
    let (cylinders, heads, sectors_per_track) = calculate_geometry(size_mb);
    let total_sectors = cylinders as u64 * heads as u64 * sectors_per_track as u64;
    let total_bytes = total_sectors * SECTOR_SIZE as u64;
    
    tracing::debug!(
        "Disk geometry: {} cylinders, {} heads, {} sectors/track = {} sectors ({} bytes)",
        cylinders, heads, sectors_per_track, total_sectors, total_bytes
    );
    
    let mut file = File::create(&expanded_path)?;
    
    // Create the MBR (sector 0)
    let mut mbr = [0u8; 512];
    
    // Add SunPCi magic at offset 12
    mbr[12..16].copy_from_slice(&SUNPCI_MAGIC.to_le_bytes());
    
    // Add revision info at offset 16
    mbr[16] = revision;  // Major version
    mbr[17] = 0;         // Minor version
    
    // Store geometry in header (offsets 18-23)
    mbr[18..20].copy_from_slice(&cylinders.to_le_bytes());
    mbr[20] = heads;
    mbr[21] = sectors_per_track;
    mbr[22..26].copy_from_slice(&(total_sectors as u32).to_le_bytes());
    
    // Create partition table entry at offset 0x1BE (446)
    // Partition 1: Primary, active, FAT16
    let partition_start: u32 = sectors_per_track as u32;  // Start after first track
    let partition_sectors: u32 = total_sectors as u32 - partition_start;
    
    // Partition entry 1
    let part_entry = &mut mbr[0x1BE..0x1CE];
    part_entry[0] = 0x80;  // Active/bootable
    
    // CHS start (head 0, sector 1, cylinder 0) - after MBR
    part_entry[1] = 1;     // Start head
    part_entry[2] = 1;     // Start sector (bits 0-5) | cylinder high (bits 6-7)
    part_entry[3] = 0;     // Start cylinder low
    
    // Partition type: FAT16 for larger disks, FAT12 for small
    part_entry[4] = if size_mb > 32 { 0x06 } else { 0x01 };  // 0x06 = FAT16, 0x01 = FAT12
    
    // CHS end
    let end_cyl = (cylinders - 1).min(1023);
    let end_head = heads - 1;
    let end_sector = sectors_per_track;
    part_entry[5] = end_head;
    part_entry[6] = (end_sector & 0x3F) | (((end_cyl >> 8) & 0x03) << 6) as u8;
    part_entry[7] = (end_cyl & 0xFF) as u8;
    
    // LBA start and size
    part_entry[8..12].copy_from_slice(&partition_start.to_le_bytes());
    part_entry[12..16].copy_from_slice(&partition_sectors.to_le_bytes());
    
    // MBR signature
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    
    // Write MBR
    file.write_all(&mbr)?;
    
    // Write FAT boot sector at partition start
    let mut boot_sector = [0u8; 512];
    
    // Jump instruction
    boot_sector[0] = 0xEB;
    boot_sector[1] = 0x3C;
    boot_sector[2] = 0x90;
    
    // OEM name
    boot_sector[3..11].copy_from_slice(b"SUNPCI  ");
    
    // BIOS Parameter Block (BPB)
    boot_sector[11..13].copy_from_slice(&512u16.to_le_bytes());  // Bytes per sector
    boot_sector[13] = if size_mb > 256 { 8 } else { 4 };         // Sectors per cluster
    boot_sector[14..16].copy_from_slice(&1u16.to_le_bytes());    // Reserved sectors
    boot_sector[16] = 2;                                          // Number of FATs
    boot_sector[17..19].copy_from_slice(&512u16.to_le_bytes());  // Root entries
    
    // Total sectors (16-bit if <= 65535, else in 32-bit field)
    if partition_sectors <= 65535 {
        boot_sector[19..21].copy_from_slice(&(partition_sectors as u16).to_le_bytes());
    } else {
        boot_sector[19..21].copy_from_slice(&0u16.to_le_bytes());
        boot_sector[32..36].copy_from_slice(&partition_sectors.to_le_bytes());
    }
    
    boot_sector[21] = 0xF8;  // Media descriptor (fixed disk)
    
    // Sectors per FAT (estimate)
    let sectors_per_fat = ((partition_sectors / boot_sector[13] as u32) * 2 / 512 + 1) as u16;
    boot_sector[22..24].copy_from_slice(&sectors_per_fat.to_le_bytes());
    
    boot_sector[24..26].copy_from_slice(&(sectors_per_track as u16).to_le_bytes());
    boot_sector[26..28].copy_from_slice(&(heads as u16).to_le_bytes());
    boot_sector[28..32].copy_from_slice(&partition_start.to_le_bytes());  // Hidden sectors
    
    // Extended boot record
    boot_sector[36] = 0x80;  // Drive number
    boot_sector[38] = 0x29;  // Extended boot signature
    boot_sector[39..43].copy_from_slice(&0x12345678u32.to_le_bytes());  // Volume serial
    boot_sector[43..54].copy_from_slice(b"NO NAME    ");  // Volume label
    boot_sector[54..62].copy_from_slice(b"FAT16   ");     // FS type
    
    // Boot signature
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;
    
    // Seek to partition start and write boot sector
    file.seek(SeekFrom::Start(partition_start as u64 * SECTOR_SIZE as u64))?;
    file.write_all(&boot_sector)?;
    
    // Initialize first FAT
    let mut fat = vec![0u8; sectors_per_fat as usize * SECTOR_SIZE as usize];
    fat[0] = 0xF8;  // Media descriptor
    fat[1] = 0xFF;
    fat[2] = 0xFF;
    fat[3] = 0xFF;
    
    // Write FAT1
    file.write_all(&fat)?;
    
    // Write FAT2
    file.write_all(&fat)?;
    
    // Write empty root directory (512 entries * 32 bytes = 16384 bytes = 32 sectors)
    let root_dir = vec![0u8; 512 * 32];
    file.write_all(&root_dir)?;
    
    // Extend file to full size
    file.seek(SeekFrom::Start(total_bytes - 1))?;
    file.write_all(&[0])?;
    
    tracing::info!("Created disk image: {} ({} MB)", expanded_path.display(), size_mb);
    Ok(())
}

/// Disk information parsed from header
struct DiskInfo {
    /// Whether this appears to be a SunPCi disk image
    is_sunpci: bool,
    /// Size in megabytes
    size_mb: u32,
    /// SunPCi format revision
    revision: u8,
    /// CHS cylinders
    cylinders: u16,
    /// CHS heads
    heads: u8,
    /// CHS sectors per track
    sectors_per_track: u8,
    /// Total sectors
    total_sectors: u64,
    /// Whether partition is bootable
    bootable: bool,
    /// Partition type description
    partition_type: String,
}

/// Expand ~ to home directory in paths
fn expand_path(path: &str) -> std::path::PathBuf {
    if path.starts_with("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            return Path::new(&home).join(&path[2..]);
        }
    }
    Path::new(path).to_path_buf()
}

/// Read and parse a disk image header
fn read_disk_header(path: &str) -> std::io::Result<DiskInfo> {
    let expanded_path = expand_path(path);
    
    let mut file = File::open(&expanded_path)?;
    let file_size = file.metadata()?.len();
    
    // Read MBR (first 512 bytes)
    let mut mbr = [0u8; 512];
    file.read_exact(&mut mbr)?;
    
    // Check for MBR signature
    if mbr[510] != 0x55 || mbr[511] != 0xAA {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "Invalid MBR signature"
        ));
    }
    
    // Check for SunPCi magic at offset 12
    let magic = u32::from_le_bytes([mbr[12], mbr[13], mbr[14], mbr[15]]);
    let is_sunpci = magic == SUNPCI_MAGIC;
    
    // Read SunPCi-specific fields if present
    let (revision, cylinders, heads, sectors_per_track, stored_sectors) = if is_sunpci {
        let rev = mbr[16];
        let cyls = u16::from_le_bytes([mbr[18], mbr[19]]);
        let heads = mbr[20];
        let spt = mbr[21];
        let sectors = u32::from_le_bytes([mbr[22], mbr[23], mbr[24], mbr[25]]);
        (rev, cyls, heads, spt, sectors as u64)
    } else {
        // Calculate geometry from file size
        let size_mb = (file_size / (1024 * 1024)) as u32;
        let (cyls, heads, spt) = calculate_geometry(size_mb);
        let sectors = file_size / SECTOR_SIZE as u64;
        (0, cyls, heads, spt, sectors)
    };
    
    // Parse partition table entry 1 (offset 0x1BE)
    let part_entry = &mbr[0x1BE..0x1CE];
    let bootable = part_entry[0] == 0x80;
    let partition_type_byte = part_entry[4];
    
    let partition_type = match partition_type_byte {
        0x00 => "Empty",
        0x01 => "FAT12",
        0x04 => "FAT16 (<32MB)",
        0x05 => "Extended",
        0x06 => "FAT16",
        0x07 => "NTFS/HPFS",
        0x0B => "FAT32",
        0x0C => "FAT32 (LBA)",
        0x0E => "FAT16 (LBA)",
        0x0F => "Extended (LBA)",
        0x82 => "Linux Swap",
        0x83 => "Linux",
        _ => "Unknown",
    }.to_string();
    
    let size_mb = (file_size / (1024 * 1024)) as u32;
    let total_sectors = if stored_sectors > 0 { stored_sectors } else { file_size / SECTOR_SIZE as u64 };
    
    Ok(DiskInfo {
        is_sunpci,
        size_mb,
        revision,
        cylinders,
        heads,
        sectors_per_track,
        total_sectors,
        bootable,
        partition_type,
    })
}
