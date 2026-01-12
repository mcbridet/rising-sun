# Floppy Drive Emulation Analysis

## Overview

The SunPCI provides floppy disk access to the guest OS through INT 13h BIOS services. The original implementation supported pass-through access to the host's physical floppy drive via Solaris block device access. For Rising Sun, we will support floppy disk image files (raw sector images).

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Windows 95/98/NT / DOS Guest                    │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  INT 13h Calls (AH=00h-08h, etc.)                            │  │
│  │  Drive 0x00 = A:, Drive 0x01 = B:                            │  │
│  └────────────────────────┬─────────────────────────────────────┘  │
│                           │ BIOS Trap                               │
│  ┌────────────────────────▼─────────────────────────────────────┐  │
│  │  Award BIOS (on SunPCI card)                                 │  │
│  │  Hooks INT 13h, forwards to host                             │  │
│  └────────────────────────┬─────────────────────────────────────┘  │
└───────────────────────────┼─────────────────────────────────────────┘
                            │ Hypervisor/Driver IPC
┌───────────────────────────┼─────────────────────────────────────────┐
│                    Host (Linux) Daemon                              │
│  ┌────────────────────────▼─────────────────────────────────────┐  │
│  │  libint13.so.1.1 - Floppy Handler                            │  │
│  │  ┌─────────────────────────────────────────────────────────┐ │  │
│  │  │  NewFloppyDisk()     - Create floppy handler            │ │  │
│  │  │  FloppyRead()        - Read sectors                     │ │  │
│  │  │  FloppyWrite()       - Write sectors                    │ │  │
│  │  │  FloppyFormat()      - Format track                     │ │  │
│  │  │  FloppyRecal()       - Recalibrate (seek track 0)       │ │  │
│  │  │  FloppyGetGeometry() - Get CHS parameters               │ │  │
│  │  │  SetFloppyParams()   - Set diskette parameters          │ │  │
│  │  └─────────────────────────────────────────────────────────┘ │  │
│  │                          │                                   │  │
│  │  ┌───────────────────────▼───────────────────────────────┐   │  │
│  │  │  Hardware Floppy Layer (HwFloppy.c)                   │   │  │
│  │  │  - HwFloppyOpen()     - Open device                   │   │  │
│  │  │  - HwFloppyRead()     - Raw read                      │   │  │
│  │  │  - HwFloppyWrite()    - Raw write                     │   │  │
│  │  │  - HwFloppyFormat()   - Format track                  │   │  │
│  │  │  - HwFloppyRecal()    - Recalibrate                   │   │  │
│  │  │  - HwFloppySetParams()- Set geometry                  │   │  │
│  │  │  - HwFloppyClose()    - Close device                  │   │  │
│  │  └───────────────────────────────────────────────────────┘   │  │
│  └──────────────────────────┬───────────────────────────────────┘  │
│                             │                                       │
│  ┌──────────────────────────▼───────────────────────────────────┐  │
│  │  Backend: Floppy Image File                                  │  │
│  │  - Raw sector image (.img, .ima, .flp)                       │  │
│  │  - 512 bytes per sector                                      │  │
│  │  - Direct file I/O                                           │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## Original SunPCI Implementation

### Library: `libint13.so.1.1` (Floppy portion)

Floppy functionality is integrated into the INT 13h library alongside hard disk and CD-ROM support.

#### Exported Floppy Symbols

| Function | Offset | Purpose |
|----------|--------|---------|
| `NewFloppyDisk` | 0x56d0 | Create floppy device handler |
| `HwFloppyOpen` | 0x637c | Open floppy device |
| `HwFloppyClose` | 0x64a4 | Close floppy device |
| `HwFloppyRead` | 0x601c | Raw sector read |
| `HwFloppyWrite` | 0x6270 | Raw sector write |
| `HwFloppyFormat` | 0x6128 | Format track |
| `HwFloppyRecal` | 0x5ea4 | Recalibrate (seek to track 0) |
| `HwFloppySetParams` | 0x5b28 | Set diskette parameters |

#### Internal Functions (from strings)

| Function | Purpose |
|----------|---------|
| `FloppyRead` | High-level read |
| `FloppyWrite` | High-level write |
| `FloppyFormat` | High-level format |
| `FloppyRecal` | High-level recalibrate |
| `FloppyGetGeometry` | Get CHS parameters |
| `FloppyGetStatus` | Get drive status |
| `FloppyCalcOffset` | CHS to file offset |
| `FloppySeek` | Seek to track |
| `FloppyDestroy` | Cleanup handler |
| `SetFloppyParams` | Set diskette type |

#### Debug Flag

| Symbol | Type | Purpose |
|--------|------|---------|
| `floppy_debug` | bool (0x1c48c) | Enable debug output |
| `FLOPPY_DEBUG` | env var | Environment variable to enable debug |

### Volume Manager Control

#### Script: `vold_floppy_disable`

**Purpose**: Disable Solaris Volume Manager control of floppy drives so SunPCI can have direct block access.

The script:
1. Backs up `/etc/vold.conf` to `/etc/vold.conf.beforesunpc`
2. Comments out `use floppy drive` line
3. Restarts volume manager (`/etc/init.d/volmgt stop/start`)

**Not needed for Rising Sun** - We use image files, not physical devices.

### Solaris Floppy Ioctls

The original used Solaris `fdio.h` ioctls:

| Ioctl | Purpose |
|-------|---------|
| `FDIOGCHAR` | Get floppy characteristics |
| `FDIOSCHAR` | Set floppy characteristics |
| `FDGETCHANGE` | Get disk change status |
| `FDRAW` | Raw FDC command |

From debug strings:
```c
"FlDevOpen() failed for device %s"
"FlDevOpen() ioctl FDIOGCHAR failed"
"FDRAW failed %x %x"
"FDRAW_REZERO failed. returning Drive Not Ready"
"HwFloppyRecal - FDGETCHANGE Status: %x Err: %x"
```

## INT 13h Floppy Functions

### Standard BIOS Functions

| AH | Function | Description |
|----|----------|-------------|
| 00h | Reset | Reset disk system |
| 01h | Status | Get last status |
| 02h | Read | Read sectors |
| 03h | Write | Write sectors |
| 04h | Verify | Verify sectors |
| 05h | Format | Format track |
| 08h | Get Params | Get drive parameters |
| 15h | Get Type | Get disk type |
| 16h | Change | Get change status |
| 17h | Set Type | Set diskette type |
| 18h | Set Media | Set media type for format |

### Drive Numbering

| Drive | Assignment |
|-------|------------|
| 0x00 | A: (first floppy) |
| 0x01 | B: (second floppy) |
| 0x80+ | Hard disks |

## Floppy Geometry

### Standard Formats

| Format | Cylinders | Heads | Sectors | Sector Size | Total Size |
|--------|-----------|-------|---------|-------------|------------|
| 360 KB | 40 | 2 | 9 | 512 | 368,640 |
| 720 KB | 80 | 2 | 9 | 512 | 737,280 |
| 1.2 MB | 80 | 2 | 15 | 512 | 1,228,800 |
| 1.44 MB | 80 | 2 | 18 | 512 | 1,474,560 |
| 2.88 MB | 80 | 2 | 36 | 512 | 2,949,120 |

### Geometry Parameters (from debug strings)

```c
"Setting geometry: ssize %d spt %d xferrate: %d"
"Set Geometry Command Failed!"
"HeadStep       = %x"
"HeadLoad       = %x"
"SectorLength   = %x"
"LastSectorNum  = %x"
"SectorGap      = %x"
"HeadSettle     = %x"
"Changing Head/Step Parameter: %x -> %x"
"Changing HeadLoad Parameter: %x -> %x"
"Changing Sector Length: %x -> %x"
"Changing Last Sector Number: %x -> %x"
"Changing Sector Gap: %x -> %x"
"Changing Format Sector Gap: %x -> %x"
"Changing Format Fill Byte: %x -> %x"
```

### BIOS Diskette Parameter Table (DPT)

Standard DPT structure (11 bytes at INT 1Eh vector):

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 1 | Step rate / head unload time |
| 1 | 1 | Head load time / DMA mode |
| 2 | 1 | Motor off delay (ticks) |
| 3 | 1 | Bytes per sector code |
| 4 | 1 | Sectors per track |
| 5 | 1 | Gap length |
| 6 | 1 | Data length |
| 7 | 1 | Format gap length |
| 8 | 1 | Format fill byte |
| 9 | 1 | Head settle time |
| 10 | 1 | Motor start time |

## Error Handling

### Debug Messages

```c
"Opening floppy failed on device %s"
"Floppy open ok on device %s"
"Door is Open. Indicating Drive Not Ready"
"FloppyCalcOffset - Why are we here?"
"Floppy Seek entered!"
"Recal Ok"
"HwFloppyRead - Track %d, head %d sector %d, nsectors %d"
"HwFloppyRead returns status code %x"
"HwFloppyWrite - Track %d, head %d sector %d, nsectors %d"
"HwFloppyWrite returns status code %x"
"HwFloppyFormat: nSectors %x SectorLength %x"
"HwFloppyClose() Restore parameters failed"
"Format Function is unimplemented"
```

### Status Codes (INT 13h)

| Code | Meaning |
|------|---------|
| 00h | Success |
| 01h | Invalid command |
| 02h | Address mark not found |
| 03h | Write protect error |
| 04h | Sector not found |
| 06h | Diskette change detected |
| 08h | DMA overrun |
| 09h | DMA boundary error |
| 0Ch | Media type not found |
| 10h | CRC error |
| 20h | Controller failure |
| 40h | Seek failure |
| 80h | Drive not ready |

## Rising Sun Implementation

### Scope

Rising Sun will support floppy disk image files only:

1. **No physical floppy pass-through**
2. **Raw sector images** (.img, .ima, .flp, .vfd)
3. **Standard geometries** (360K, 720K, 1.2M, 1.44M, 2.88M)
4. **Read/write support**
5. **Format support** (within image file)

### Floppy Image Backend

```rust
pub struct FloppyGeometry {
    pub cylinders: u8,
    pub heads: u8,
    pub sectors_per_track: u8,
    pub sector_size: u16,
}

impl FloppyGeometry {
    /// Detect geometry from file size
    pub fn from_size(size: u64) -> Option<Self>;
    
    /// Get total sectors
    pub fn total_sectors(&self) -> u32;
    
    /// CHS to LBA conversion
    pub fn chs_to_lba(&self, c: u8, h: u8, s: u8) -> u32;
    
    /// LBA to file offset
    pub fn lba_to_offset(&self, lba: u32) -> u64;
}

pub struct FloppyBackend {
    /// Path to image file
    image_path: PathBuf,
    
    /// Open file handle
    file: Option<File>,
    
    /// Detected geometry
    geometry: FloppyGeometry,
    
    /// Write protect flag
    write_protected: bool,
    
    /// Media present flag
    media_present: bool,
    
    /// Media changed flag (for change line)
    media_changed: bool,
}

impl FloppyBackend {
    /// Mount a floppy image
    pub fn mount(&mut self, path: &Path) -> Result<()>;
    
    /// Unmount current image
    pub fn unmount(&mut self) -> Result<()>;
    
    /// Read sectors
    pub fn read_sectors(&self, c: u8, h: u8, s: u8, count: u8, buffer: &mut [u8]) -> Result<u8>;
    
    /// Write sectors
    pub fn write_sectors(&mut self, c: u8, h: u8, s: u8, count: u8, buffer: &[u8]) -> Result<u8>;
    
    /// Format track
    pub fn format_track(&mut self, c: u8, h: u8) -> Result<u8>;
    
    /// Get geometry
    pub fn get_geometry(&self) -> &FloppyGeometry;
    
    /// Check if media present
    pub fn is_media_present(&self) -> bool;
    
    /// Check/clear media changed flag
    pub fn check_media_changed(&mut self) -> bool;
}
```

### INT 13h Handler

```rust
pub struct Int13FloppyHandler {
    /// Floppy backend
    backend: FloppyBackend,
    
    /// Drive number (0 or 1)
    drive_num: u8,
    
    /// Last status code
    last_status: u8,
    
    /// Current head position (for seek emulation)
    current_track: u8,
}

impl Int13FloppyHandler {
    /// Handle INT 13h function
    pub fn handle_function(&mut self, regs: &mut Registers) -> u8 {
        match regs.ah {
            0x00 => self.reset(),
            0x01 => self.get_status(),
            0x02 => self.read_sectors(regs),
            0x03 => self.write_sectors(regs),
            0x04 => self.verify_sectors(regs),
            0x05 => self.format_track(regs),
            0x08 => self.get_parameters(regs),
            0x15 => self.get_disk_type(regs),
            0x16 => self.get_change_status(regs),
            0x17 => self.set_diskette_type(regs),
            0x18 => self.set_media_type(regs),
            _ => 0x01, // Invalid function
        }
    }
}
```

### Geometry Detection

```rust
impl FloppyGeometry {
    pub fn from_size(size: u64) -> Option<Self> {
        match size {
            368_640 => Some(Self::new(40, 2, 9, 512)),   // 360 KB
            737_280 => Some(Self::new(80, 2, 9, 512)),   // 720 KB
            1_228_800 => Some(Self::new(80, 2, 15, 512)), // 1.2 MB
            1_474_560 => Some(Self::new(80, 2, 18, 512)), // 1.44 MB
            2_949_120 => Some(Self::new(80, 2, 36, 512)), // 2.88 MB
            _ => None, // Unknown format
        }
    }
}
```

### Configuration

```toml
[floppy]
# Drive A: image (optional)
drive_a = "/path/to/boot.img"
drive_a_write_protect = false

# Drive B: image (optional, uncommon)
# drive_b = "/path/to/data.img"
# drive_b_write_protect = true
```

### UI Integration

The Qt6 frontend will provide:
- Floppy image file selection dialog
- Mount/unmount buttons for A: and B:
- Write-protect checkbox
- Create blank floppy image option
- Current image path display

## Differences: Original vs Rising Sun

| Feature | Original SunPCI | Rising Sun |
|---------|-----------------|------------|
| Physical floppy | ✓ Pass-through | ✗ Not supported |
| Image files | ✗ Not supported | ✓ Primary feature |
| Volume manager | ✓ vold_floppy_disable | ✗ Not needed |
| Format support | ✓ Physical format | ✓ Image format only |
| Write protect | ✓ Physical tab | ✓ Software flag |
| Media change | ✓ Hardware detect | ✓ Software detect |
| Boot from floppy | ✓ BIOS boot | ✓ BIOS boot |

## Implementation Priority

### Phase 1: Basic Image Support
1. Image file mounting
2. Geometry detection from file size
3. Read sectors (INT 13h AH=02h)
4. Write sectors (INT 13h AH=03h)
5. Get parameters (INT 13h AH=08h)

### Phase 2: Full Compatibility
1. Reset (AH=00h)
2. Get status (AH=01h)
3. Verify (AH=04h)
4. Media change detection (AH=16h)
5. Set diskette type (AH=17h, 18h)

### Phase 3: Format Support
1. Format track (AH=05h) - fill with 0xF6
2. Create blank image utility

## DOS Utilities in Package

The package includes OpenDOS 7.01 utilities relevant to floppy:

| Utility | Purpose |
|---------|---------|
| `fdisk.com` | Partition tool |
| `format.com` | Format disks |
| `diskcopy.com` | Copy disk |
| `diskcomp.com` | Compare disks |
| `sys.com` | Make bootable |
| `label.com` | Volume label |

## Open Questions

1. Should we support creating blank floppy images from the UI?
2. Is DMF (Distribution Media Format, 1.68 MB) support needed?
3. Should we support non-standard sector sizes for copy protection?
4. Is B: drive (second floppy) commonly used enough to implement?

## References

- BIOS INT 13h Specification
- Floppy Disk Controller (FDC) documentation (Intel 8272A / NEC µPD765)
- PC System Architecture: Floppy Disk Subsystem
