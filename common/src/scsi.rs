//! SCSI types and constants for CD-ROM emulation.
//!
//! This module provides SCSI-2/MMC-2 command definitions for virtual CD-ROM
//! emulation. The implementation focuses on ISO 9660 image file support.

// ============================================================================
// SCSI Command Opcodes (SPC-2 / MMC-2)
// ============================================================================

/// SCSI command opcodes for CD-ROM devices
pub mod opcode {
    /// Test if the logical unit is ready
    pub const TEST_UNIT_READY: u8 = 0x00;
    /// Request sense data from previous command
    pub const REQUEST_SENSE: u8 = 0x03;
    /// Return device identification
    pub const INQUIRY: u8 = 0x12;
    /// Return mode parameters
    pub const MODE_SENSE_6: u8 = 0x1A;
    /// Prevent/allow medium removal
    pub const PREVENT_ALLOW_MEDIUM_REMOVAL: u8 = 0x1E;
    /// Return logical block address capacity
    pub const READ_CAPACITY: u8 = 0x25;
    /// Read data from medium (10-byte CDB)
    pub const READ_10: u8 = 0x28;
    /// Seek to logical block address
    pub const SEEK_10: u8 = 0x2B;
    /// Read table of contents
    pub const READ_TOC: u8 = 0x43;
    /// Get configuration (for feature discovery)
    pub const GET_CONFIGURATION: u8 = 0x46;
    /// Get event/status notification
    pub const GET_EVENT_STATUS_NOTIFICATION: u8 = 0x4A;
    /// Read disc information
    pub const READ_DISC_INFORMATION: u8 = 0x51;
    /// Return mode parameters (10-byte)
    pub const MODE_SENSE_10: u8 = 0x5A;
    /// Read data from medium (12-byte CDB)
    pub const READ_12: u8 = 0xA8;
    /// Report key (for CSS/CPRM)
    pub const REPORT_KEY: u8 = 0xA4;
    /// Mechanism status
    pub const MECHANISM_STATUS: u8 = 0xBD;
    /// Read CD (MMC)
    pub const READ_CD: u8 = 0xBE;
}

// ============================================================================
// SCSI Status Codes
// ============================================================================

/// SCSI status codes returned in command response
pub mod status {
    /// Command completed successfully
    pub const GOOD: u8 = 0x00;
    /// Check condition - sense data available
    pub const CHECK_CONDITION: u8 = 0x02;
    /// Condition met
    pub const CONDITION_MET: u8 = 0x04;
    /// Device busy
    pub const BUSY: u8 = 0x08;
    /// Reservation conflict
    pub const RESERVATION_CONFLICT: u8 = 0x18;
    /// Task set full
    pub const TASK_SET_FULL: u8 = 0x28;
    /// ACA active
    pub const ACA_ACTIVE: u8 = 0x30;
    /// Task aborted
    pub const TASK_ABORTED: u8 = 0x40;
}

// ============================================================================
// Sense Key Codes
// ============================================================================

/// Sense key codes for error reporting
pub mod sense_key {
    /// No error
    pub const NO_SENSE: u8 = 0x00;
    /// Recovered error
    pub const RECOVERED_ERROR: u8 = 0x01;
    /// Device not ready
    pub const NOT_READY: u8 = 0x02;
    /// Medium error
    pub const MEDIUM_ERROR: u8 = 0x03;
    /// Hardware error
    pub const HARDWARE_ERROR: u8 = 0x04;
    /// Illegal request
    pub const ILLEGAL_REQUEST: u8 = 0x05;
    /// Unit attention (media changed, reset, etc.)
    pub const UNIT_ATTENTION: u8 = 0x06;
    /// Data protect (write protected)
    pub const DATA_PROTECT: u8 = 0x07;
    /// Blank check
    pub const BLANK_CHECK: u8 = 0x08;
    /// Aborted command
    pub const ABORTED_COMMAND: u8 = 0x0B;
}

// ============================================================================
// Additional Sense Codes (ASC/ASCQ)
// ============================================================================

/// Additional Sense Codes for detailed error information
pub mod asc {
    /// No additional sense information
    pub const NO_ADDITIONAL_SENSE: u8 = 0x00;
    /// Logical unit not ready, cause not reportable
    pub const LUN_NOT_READY: u8 = 0x04;
    /// Medium not present
    pub const MEDIUM_NOT_PRESENT: u8 = 0x3A;
    /// Invalid command operation code
    pub const INVALID_COMMAND: u8 = 0x20;
    /// Logical block address out of range
    pub const LBA_OUT_OF_RANGE: u8 = 0x21;
    /// Invalid field in CDB
    pub const INVALID_FIELD_IN_CDB: u8 = 0x24;
    /// Power on, reset, or bus device reset occurred
    pub const POWER_ON_RESET: u8 = 0x29;
    /// Parameters changed
    pub const PARAMETERS_CHANGED: u8 = 0x2A;
    /// Not ready to ready transition (medium may have changed)
    pub const MEDIUM_MAY_HAVE_CHANGED: u8 = 0x28;
}

/// Additional Sense Code Qualifiers
pub mod ascq {
    /// No qualifier
    pub const NONE: u8 = 0x00;
    /// Becoming ready
    pub const BECOMING_READY: u8 = 0x01;
    /// Medium not present - tray closed
    pub const MEDIUM_NOT_PRESENT_TRAY_CLOSED: u8 = 0x01;
    /// Medium not present - tray open
    pub const MEDIUM_NOT_PRESENT_TRAY_OPEN: u8 = 0x02;
    /// Power on occurred
    pub const POWER_ON_OCCURRED: u8 = 0x00;
    /// SCSI bus reset occurred
    pub const BUS_RESET_OCCURRED: u8 = 0x02;
}

// ============================================================================
// Device Type Codes
// ============================================================================

/// Peripheral device type codes
pub mod device_type {
    /// Direct-access device (disk)
    pub const DISK: u8 = 0x00;
    /// Sequential-access device (tape)
    pub const TAPE: u8 = 0x01;
    /// CD-ROM/DVD device
    pub const CDROM: u8 = 0x05;
    /// Optical memory device
    pub const OPTICAL: u8 = 0x07;
    /// Medium changer
    pub const CHANGER: u8 = 0x08;
}

// ============================================================================
// Mode Page Codes
// ============================================================================

/// Mode page codes for MODE SENSE/SELECT
pub mod mode_page {
    /// Read/Write error recovery
    pub const RW_ERROR_RECOVERY: u8 = 0x01;
    /// CD device parameters
    pub const CD_DEVICE_PARAMETERS: u8 = 0x0D;
    /// CD audio control
    pub const CD_AUDIO_CONTROL: u8 = 0x0E;
    /// Power condition
    pub const POWER_CONDITION: u8 = 0x1A;
    /// Capabilities and mechanical status
    pub const CAPABILITIES: u8 = 0x2A;
    /// Return all mode pages
    pub const ALL_PAGES: u8 = 0x3F;
}

// ============================================================================
// Data Structures
// ============================================================================

/// Fixed-format sense data (18 bytes)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct SenseData {
    /// Response code (0x70 = current, 0x71 = deferred)
    pub response_code: u8,
    /// Obsolete
    pub obsolete: u8,
    /// Sense key, with flags in high nibble
    pub sense_key: u8,
    /// Information bytes (command-specific)
    pub information: [u8; 4],
    /// Additional sense length (should be 10 for fixed format)
    pub additional_length: u8,
    /// Command-specific information
    pub command_specific: [u8; 4],
    /// Additional sense code
    pub asc: u8,
    /// Additional sense code qualifier
    pub ascq: u8,
    /// Field replaceable unit code
    pub fruc: u8,
    /// Sense key specific bytes
    pub sense_key_specific: [u8; 3],
}

impl SenseData {
    /// Size of fixed-format sense data
    pub const SIZE: usize = 18;

    /// Create sense data indicating no error
    pub fn no_sense() -> Self {
        Self {
            response_code: 0x70,
            additional_length: 10,
            ..Default::default()
        }
    }

    /// Create sense data for a given error condition
    pub fn new(sense_key: u8, asc: u8, ascq: u8) -> Self {
        Self {
            response_code: 0x70,
            sense_key,
            additional_length: 10,
            asc,
            ascq,
            ..Default::default()
        }
    }

    /// Create "not ready, medium not present" sense data
    pub fn medium_not_present() -> Self {
        Self::new(
            sense_key::NOT_READY,
            asc::MEDIUM_NOT_PRESENT,
            ascq::MEDIUM_NOT_PRESENT_TRAY_CLOSED,
        )
    }

    /// Create "unit attention, medium may have changed" sense data
    pub fn medium_changed() -> Self {
        Self::new(
            sense_key::UNIT_ATTENTION,
            asc::MEDIUM_MAY_HAVE_CHANGED,
            ascq::NONE,
        )
    }

    /// Create "illegal request, invalid command" sense data
    pub fn invalid_command() -> Self {
        Self::new(sense_key::ILLEGAL_REQUEST, asc::INVALID_COMMAND, ascq::NONE)
    }

    /// Create "illegal request, invalid field in CDB" sense data
    pub fn invalid_field() -> Self {
        Self::new(
            sense_key::ILLEGAL_REQUEST,
            asc::INVALID_FIELD_IN_CDB,
            ascq::NONE,
        )
    }

    /// Create "illegal request, LBA out of range" sense data
    pub fn lba_out_of_range() -> Self {
        Self::new(
            sense_key::ILLEGAL_REQUEST,
            asc::LBA_OUT_OF_RANGE,
            ascq::NONE,
        )
    }

    /// Serialize sense data to a buffer
    pub fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut buf = [0u8; Self::SIZE];
        buf[0] = self.response_code;
        buf[1] = self.obsolete;
        buf[2] = self.sense_key;
        buf[3..7].copy_from_slice(&self.information);
        buf[7] = self.additional_length;
        buf[8..12].copy_from_slice(&self.command_specific);
        buf[12] = self.asc;
        buf[13] = self.ascq;
        buf[14] = self.fruc;
        buf[15..18].copy_from_slice(&self.sense_key_specific);
        buf
    }
}

/// INQUIRY response data (standard 36 bytes)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct InquiryData {
    /// Peripheral qualifier and device type
    pub peripheral: u8,
    /// Removable media bit (0x80) and device type modifier
    pub rmb: u8,
    /// ISO/ECMA/ANSI version
    pub version: u8,
    /// Response data format and flags
    pub response_format: u8,
    /// Additional length (31 for 36-byte response)
    pub additional_length: u8,
    /// Reserved/flags
    pub flags: [u8; 3],
    /// Vendor identification (8 bytes, space-padded)
    pub vendor: [u8; 8],
    /// Product identification (16 bytes, space-padded)
    pub product: [u8; 16],
    /// Product revision (4 bytes, space-padded)
    pub revision: [u8; 4],
}

impl Default for InquiryData {
    fn default() -> Self {
        Self::new()
    }
}

impl InquiryData {
    /// Standard size of INQUIRY response
    pub const SIZE: usize = 36;

    /// Create a new INQUIRY response for a virtual CD-ROM drive
    pub fn new() -> Self {
        Self {
            peripheral: device_type::CDROM, // CD-ROM device
            rmb: 0x80,                      // Removable media
            version: 0x02,                  // SCSI-2 compliant
            response_format: 0x02,          // Response format 2
            additional_length: 31,          // 36 - 5 = 31
            flags: [0, 0, 0],
            vendor: *b"SUN     ",
            product: *b"Virtual CDROM   ",
            revision: *b"1.0 ",
        }
    }

    /// Create with custom vendor/product strings
    pub fn with_identity(vendor: &str, product: &str, revision: &str) -> Self {
        let mut data = Self::new();

        // Copy vendor (8 bytes, space-padded)
        let vendor_bytes = vendor.as_bytes();
        let vendor_len = vendor_bytes.len().min(8);
        data.vendor = *b"        ";
        data.vendor[..vendor_len].copy_from_slice(&vendor_bytes[..vendor_len]);

        // Copy product (16 bytes, space-padded)
        let product_bytes = product.as_bytes();
        let product_len = product_bytes.len().min(16);
        data.product = *b"                ";
        data.product[..product_len].copy_from_slice(&product_bytes[..product_len]);

        // Copy revision (4 bytes, space-padded)
        let revision_bytes = revision.as_bytes();
        let revision_len = revision_bytes.len().min(4);
        data.revision = *b"    ";
        data.revision[..revision_len].copy_from_slice(&revision_bytes[..revision_len]);

        data
    }

    /// Serialize to bytes
    pub fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut buf = [0u8; Self::SIZE];
        buf[0] = self.peripheral;
        buf[1] = self.rmb;
        buf[2] = self.version;
        buf[3] = self.response_format;
        buf[4] = self.additional_length;
        buf[5..8].copy_from_slice(&self.flags);
        buf[8..16].copy_from_slice(&self.vendor);
        buf[16..32].copy_from_slice(&self.product);
        buf[32..36].copy_from_slice(&self.revision);
        buf
    }
}

/// READ CAPACITY response (8 bytes)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct ReadCapacityData {
    /// Last logical block address (big-endian)
    pub last_lba: [u8; 4],
    /// Block length in bytes (big-endian)
    pub block_length: [u8; 4],
}

impl ReadCapacityData {
    /// Size of READ CAPACITY response
    pub const SIZE: usize = 8;

    /// Create from total sectors and sector size
    pub fn new(total_sectors: u32, sector_size: u32) -> Self {
        let last_lba = if total_sectors > 0 {
            total_sectors - 1
        } else {
            0
        };
        Self {
            last_lba: last_lba.to_be_bytes(),
            block_length: sector_size.to_be_bytes(),
        }
    }

    /// Serialize to bytes
    pub fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut buf = [0u8; Self::SIZE];
        buf[0..4].copy_from_slice(&self.last_lba);
        buf[4..8].copy_from_slice(&self.block_length);
        buf
    }
}

/// TOC (Table of Contents) entry for READ TOC
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct TocEntry {
    /// Reserved
    pub reserved1: u8,
    /// ADR and control bits
    pub adr_control: u8,
    /// Track number
    pub track_number: u8,
    /// Reserved
    pub reserved2: u8,
    /// Track start address (LBA, big-endian)
    pub start_address: [u8; 4],
}

impl TocEntry {
    /// Create a data track entry
    pub fn data_track(track_number: u8, start_lba: u32) -> Self {
        Self {
            reserved1: 0,
            adr_control: 0x14, // ADR=1 (Q sub-channel), Control=4 (data track)
            track_number,
            reserved2: 0,
            start_address: start_lba.to_be_bytes(),
        }
    }

    /// Create a lead-out track entry (track AA)
    pub fn lead_out(total_sectors: u32) -> Self {
        Self {
            reserved1: 0,
            adr_control: 0x14,
            track_number: 0xAA, // Lead-out
            reserved2: 0,
            start_address: total_sectors.to_be_bytes(),
        }
    }
}

/// READ TOC response header
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct TocHeader {
    /// TOC data length (big-endian, excludes this field)
    pub data_length: [u8; 2],
    /// First track number
    pub first_track: u8,
    /// Last track number
    pub last_track: u8,
}

/// Simple single-track TOC for a data CD
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct SimpleToc {
    /// TOC header
    pub header: TocHeader,
    /// Track 1 entry
    pub track1: TocEntry,
    /// Lead-out entry
    pub lead_out: TocEntry,
}

impl SimpleToc {
    /// Size of simple TOC response
    pub const SIZE: usize = 4 + 8 + 8; // header + 2 entries

    /// Create a simple TOC for a data CD with given size
    pub fn new(total_sectors: u32) -> Self {
        Self {
            header: TocHeader {
                // Length excludes the data_length field itself (2 bytes)
                // 2 (remaining header) + 16 (2 entries) = 18
                data_length: 18u16.to_be_bytes(),
                first_track: 1,
                last_track: 1,
            },
            track1: TocEntry::data_track(1, 0),
            lead_out: TocEntry::lead_out(total_sectors),
        }
    }

    /// Serialize to bytes
    pub fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut buf = [0u8; Self::SIZE];
        buf[0..2].copy_from_slice(&self.header.data_length);
        buf[2] = self.header.first_track;
        buf[3] = self.header.last_track;
        // Track 1
        buf[4] = self.track1.reserved1;
        buf[5] = self.track1.adr_control;
        buf[6] = self.track1.track_number;
        buf[7] = self.track1.reserved2;
        buf[8..12].copy_from_slice(&self.track1.start_address);
        // Lead-out
        buf[12] = self.lead_out.reserved1;
        buf[13] = self.lead_out.adr_control;
        buf[14] = self.lead_out.track_number;
        buf[15] = self.lead_out.reserved2;
        buf[16..20].copy_from_slice(&self.lead_out.start_address);
        buf
    }
}

// ============================================================================
// SCSI Command Result
// ============================================================================

/// Result of a SCSI command execution
#[derive(Debug, Clone)]
pub enum ScsiResult {
    /// Command completed successfully with data
    Good(Vec<u8>),
    /// Command completed with no data transfer
    GoodNoData,
    /// Check condition - caller should request sense
    CheckCondition(SenseData),
}

impl ScsiResult {
    /// Get the SCSI status code
    pub fn status(&self) -> u8 {
        match self {
            ScsiResult::Good(_) | ScsiResult::GoodNoData => status::GOOD,
            ScsiResult::CheckCondition(_) => status::CHECK_CONDITION,
        }
    }
}

// ============================================================================
// CD-ROM Sector Size Constants
// ============================================================================

/// Standard CD-ROM sector size (Mode 1 data)
pub const SECTOR_SIZE_CDROM: u32 = 2048;

/// CD-ROM sector size with EDC/ECC (Mode 1 raw)
pub const SECTOR_SIZE_CDROM_RAW: u32 = 2352;

// ============================================================================
// Utility Functions
// ============================================================================

/// Extract LBA from a 10-byte CDB (bytes 2-5, big-endian)
pub fn cdb10_get_lba(cdb: &[u8]) -> u32 {
    if cdb.len() < 6 {
        return 0;
    }
    u32::from_be_bytes([cdb[2], cdb[3], cdb[4], cdb[5]])
}

/// Extract transfer length from a 10-byte CDB (bytes 7-8, big-endian)
pub fn cdb10_get_length(cdb: &[u8]) -> u16 {
    if cdb.len() < 9 {
        return 0;
    }
    u16::from_be_bytes([cdb[7], cdb[8]])
}

/// Extract allocation length from INQUIRY CDB (byte 4)
pub fn inquiry_get_alloc_length(cdb: &[u8]) -> u8 {
    if cdb.len() < 5 {
        return 0;
    }
    cdb[4]
}

/// Extract allocation length from MODE SENSE(6) CDB (byte 4)
pub fn mode_sense6_get_alloc_length(cdb: &[u8]) -> u8 {
    if cdb.len() < 5 {
        return 0;
    }
    cdb[4]
}

/// Extract page code from MODE SENSE CDB (byte 2, bits 0-5)
pub fn mode_sense_get_page_code(cdb: &[u8]) -> u8 {
    if cdb.len() < 3 {
        return 0;
    }
    cdb[2] & 0x3F
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sense_data_size() {
        assert_eq!(std::mem::size_of::<SenseData>(), SenseData::SIZE);
    }

    #[test]
    fn test_inquiry_data_size() {
        assert_eq!(std::mem::size_of::<InquiryData>(), InquiryData::SIZE);
    }

    #[test]
    fn test_read_capacity_size() {
        assert_eq!(
            std::mem::size_of::<ReadCapacityData>(),
            ReadCapacityData::SIZE
        );
    }

    #[test]
    fn test_inquiry_identity() {
        let inq = InquiryData::with_identity("RISING", "Virtual CDROM", "2.0");
        assert_eq!(&inq.vendor, b"RISING  ");
        assert_eq!(&inq.product, b"Virtual CDROM   ");
        assert_eq!(&inq.revision, b"2.0 ");
    }

    #[test]
    fn test_cdb10_parsing() {
        // READ(10) CDB: opcode, flags, LBA[4], group, length[2], control
        let cdb = [0x28, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00];
        assert_eq!(cdb10_get_lba(&cdb), 0x00010000);
        assert_eq!(cdb10_get_length(&cdb), 0x0010);
    }

    #[test]
    fn test_simple_toc() {
        let toc = SimpleToc::new(333000); // ~650 MB CD
        let bytes = toc.to_bytes();
        assert_eq!(bytes.len(), SimpleToc::SIZE);
        // First track should be 1
        assert_eq!(bytes[2], 1);
        // Last track should be 1
        assert_eq!(bytes[3], 1);
        // Track 1 number
        assert_eq!(bytes[6], 1);
        // Lead-out track number
        assert_eq!(bytes[14], 0xAA);
    }
}
