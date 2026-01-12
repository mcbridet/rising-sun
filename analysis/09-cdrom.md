# CD-ROM Emulation Analysis

## Overview

The SunPCi provides CD-ROM access to the guest OS through SCSI emulation integrated into the INT 13h disk services library. Unlike the original implementation which supported both physical CD-ROM pass-through and auto-mounting via Solaris Volume Manager, the Rising Sun implementation will focus exclusively on ISO file mounting.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Windows 95/98/NT Guest                          │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  MSCDEX.EXE / CD-ROM Driver                                  │  │
│  │  (Standard Microsoft CD Extensions)                          │  │
│  └────────────────────────┬─────────────────────────────────────┘  │
│                           │ INT 13h / SCSI Calls                    │
│  ┌────────────────────────▼─────────────────────────────────────┐  │
│  │  BIOS CD-ROM Functions                                       │  │
│  │  (El Torito boot, SCSI pass-through)                         │  │
│  └────────────────────────┬─────────────────────────────────────┘  │
└───────────────────────────┼─────────────────────────────────────────┘
                            │ Hypervisor Trap
┌───────────────────────────┼─────────────────────────────────────────┐
│                    Host (Linux) Daemon                              │
│  ┌────────────────────────▼─────────────────────────────────────┐  │
│  │  libint13.so.1.1                                             │  │
│  │  ┌─────────────────────────────────────────────────────────┐ │  │
│  │  │  NewCDRom() - CD-ROM device handler constructor         │ │  │
│  │  │  CDRead()   - Read sectors (2048-byte)                  │ │  │
│  │  │  CDWrite()  - Write sectors (CD-RW, if supported)       │ │  │
│  │  │  CDSendScsiCmd() - SCSI CDB pass-through                │ │  │
│  │  │  CdromEject()    - Eject/close tray                     │ │  │
│  │  └─────────────────────────────────────────────────────────┘ │  │
│  └────────────────────────┬─────────────────────────────────────┘  │
│                           │                                         │
│  ┌────────────────────────▼─────────────────────────────────────┐  │
│  │  ISO File Backend                                            │  │
│  │  - Standard ISO 9660 file read                               │  │
│  │  - Joliet extension support                                  │  │
│  │  - 2048-byte sector access                                   │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## Original SunPCi Implementation

### Host Components

#### Library: `libint13.so.1.1` (CD-ROM portion)

The CD-ROM functionality is integrated into the INT 13h library, not a separate library.

| Function | Offset | Purpose |
|----------|--------|---------|
| `NewCDRom` | 0x8470 | Create CD-ROM device handler |
| `CDRead` | 0x6f20 | Read sectors from CD-ROM |
| `CDWrite` | 0x6e30 | Write sectors (CD-RW support) |
| `CDSendScsiCmd` | 0x78fc | SCSI CDB pass-through |
| `CdromEject` | 0x6c04 | Eject CD-ROM tray |
| `PrintScsiResults` | 0x728c | Debug SCSI results |

#### Dummy Handlers (error stubs)
| Function | Purpose |
|----------|---------|
| `NoHDSendScsiCmd` | "This should not be called for a hard disk device" |
| `NoHDSendScsiIoctl` | "This should not be called for a hard disk device" |
| `NoFlSendScsiCmd` | "This should not be called for a floppy device" |
| `NoFlSendScsiIoctl` | "This should not be called for a floppy device" |

#### Volume Manager Integration: `action_sunpci.so.1` (12 KB)

**Purpose**: Hooks into Solaris `rmmount` to notify SunPCi when CDs are inserted/ejected.

**NOT NEEDED for Rising Sun** - We're implementing ISO-only mounting.

Key strings from action_sunpci.so:
```c
"action_sunpci: devicename %s symdev %s"
"action_sunpci: couldn't find any SunPCi compatible devices"
"action_sunpci: couldn't open device %s"
"action_sunpci: VOLIF_INSERT ioctl failed"
"action_sunpci: VOLIF_EJECT ioctl failed"
```

Actions handled:
- `insert` - CD inserted, notify guest via ioctl
- `eject` - CD ejected, notify guest
- `notify` - State change notification

### Guest Components

#### DOS: NWCDEX.EXE

Located at: `defaults/7.01/dos/nwcdex.exe`
- PKZIP self-extracting archive
- MSCDEX-compatible CD-ROM extension for DOS
- Provides drive letter assignment for CD-ROM

#### Windows 95/98/NT

Uses standard Windows CD-ROM drivers - no SunPCi-specific drivers needed for CD access.

## SCSI Emulation Details

### SCSI Command Handling

From libint13 debug strings:

```c
"libint13: SendScsiCmd() Drive %d CdbLen %d, WriteLen: %d ReadLen %d"
"libint13: ScsiIoctl() Drive %d Ioctl Code %x"
"CDSendScsiCmd CdbLen %d, SenseLen %d WriteLen: %d ReadLen %d"
"Unknown SCSI Command 0x%x"
```

### Supported SCSI Ioctls

| Ioctl | Purpose |
|-------|---------|
| `SCSI_DEVLOAD_IOCTL` | Device load (CD inserted) |
| `SCSI_DEVUNLOAD_IOCTL` | Device unload (CD ejected) |
| `SCSI_EJECT_IOCTL` | Eject tray |
| `SCSI_VOLCHANGE_IOCTL` | Media change notification |
| `SCSI_DISKTYPE_IOCTL` | Get disk type |
| `SCSI_PLAY_IOCTL` | Audio CD play |
| `SCSI_STOP_IOCTL` | Stop playback |
| `SCSI_PAUSE/RESUME_IOCTL` | Pause/resume audio |
| `SCSI_SETVOL_IOCTL` | Set audio volume |

### SCSI Command Support

From `CDSendScsiCmd`:
```c
"Cmd-40-O: Change Definition (set scsi version)"
```

The code processes standard SCSI-2 commands for CD-ROM access.

### USCSICMD Structure (Solaris)

```c
struct uscsi_cmd {
    int     uscsi_flags;
    short   uscsi_status;
    short   uscsi_timeout;
    caddr_t uscsi_cdb;
    caddr_t uscsi_bufaddr;
    size_t  uscsi_buflen;
    size_t  uscsi_resid;
    uchar_t uscsi_cdblen;
    uchar_t uscsi_rqlen;
    uchar_t uscsi_rqstatus;
    uchar_t uscsi_rqresid;
    caddr_t uscsi_rqbuf;
};
```

Error handling output:
```c
"USCSICMD ioctl failed!!!!"
"uscsi_status = %x"
"uscsi_rqstatus = %x"
"uscsi_rqresid = %x"
"uscsi_resid = %x"
"uscsi_cdb = %x"
"uscsi_cdblen = %x"
"uscsi_bufaddr = %x"
"uscsi_buflen = %x"
"uscsi_flags = %x"
```

## Media Change Detection

### State Machine

```c
"ChangeState: The CD is inserted and the device is closed. opening"
"ChangeState: The CD is removed and the device is open. closing"
"ChangeState: The CD is inserted and the device is open. do nothing"
"Door is Open. Indicating Drive Not Ready"
```

### Media Change Interrupt

```c
"SCSI_VOLCHANGE_IOCTL Posting Media Change Interrupt"
"SCSI_VOLCHANGE_IOCTL NO Media Change Interrupt"
```

## Configuration

### SunPCi.ini Reference

```c
"sunpci: Your SunPCi.ini file does not specify a CD device"
```

The CD device path was configured in the SunPCi.ini configuration file.

### Global Variables

| Symbol | Type | Purpose |
|--------|------|---------|
| `cd_device` | char* (0x1c494) | Path to CD-ROM device |
| `VolmgrDevice` | import | Volume manager device path |

## Error Handling

### Eject Errors
```c
"cannot eject CD-ROM"
"sunpci: filesystem is mounted, can't eject"
"sunpci: EJECT failed. The filesystem is probably mounted"
"sunpci: Can't eject cd. Is the tray open?"
"CDDestroy: CD Device wasn't open. Not closing"
```

## Rising Sun Implementation

### Scope (ISO-Only)

The Rising Sun implementation will **only** support ISO file mounting:

1. **No physical CD-ROM pass-through**
2. **No auto-mount integration**
3. **No audio CD playback** (no SCSI audio ioctls)

### Required SCSI Commands for ISO

| Command | OpCode | Purpose |
|---------|--------|---------|
| TEST UNIT READY | 0x00 | Check if media present |
| REQUEST SENSE | 0x03 | Get error information |
| INQUIRY | 0x12 | Device identification |
| MODE SENSE | 0x1A/0x5A | Get device parameters |
| READ CAPACITY | 0x25 | Get disc size |
| READ(10) | 0x28 | Read sectors |
| READ TOC | 0x43 | Read table of contents |

### ISO File Backend

```rust
pub struct IsoBackend {
    /// Path to ISO file
    iso_path: PathBuf,
    
    /// Open file handle
    file: Option<File>,
    
    /// ISO 9660 volume descriptor
    volume_descriptor: Option<VolumeDescriptor>,
    
    /// Total sectors (2048 bytes each)
    total_sectors: u32,
    
    /// Media present flag
    media_present: bool,
}

impl IsoBackend {
    /// Mount an ISO file
    pub fn mount(&mut self, path: &Path) -> Result<()>;
    
    /// Unmount current ISO
    pub fn unmount(&mut self) -> Result<()>;
    
    /// Read sectors from ISO
    pub fn read_sectors(&self, lba: u32, count: u16, buffer: &mut [u8]) -> Result<usize>;
    
    /// Get media info
    pub fn get_capacity(&self) -> Option<(u32, u32)>;  // (sectors, sector_size)
    
    /// Check if media present
    pub fn is_media_present(&self) -> bool;
}
```

### SCSI Emulation Layer

```rust
pub struct ScsiCdrom {
    /// ISO backend
    backend: IsoBackend,
    
    /// Last sense data
    sense_data: SenseData,
    
    /// Drive geometry/parameters
    mode_pages: ModePages,
}

impl ScsiCdrom {
    /// Process SCSI CDB
    pub fn execute_command(&mut self, cdb: &[u8], data: &mut [u8]) -> ScsiResult;
    
    /// Handle media change
    pub fn set_media_changed(&mut self);
    
    /// Eject (unmount ISO)
    pub fn eject(&mut self) -> Result<()>;
}
```

### BIOS Integration

The CD-ROM will be exposed as a BIOS drive for El Torito boot support:

```rust
pub struct Int13CdromHandler {
    /// SCSI emulation layer
    scsi: ScsiCdrom,
    
    /// Drive number (usually 0xE0 for first CD)
    drive_num: u8,
    
    /// Boot catalog (if bootable ISO)
    boot_catalog: Option<ElToritoCatalog>,
}

impl Int13CdromHandler {
    /// Handle INT 13h AH=4x (CD-ROM extensions)
    pub fn handle_cdrom_function(&mut self, ah: u8, regs: &mut Registers) -> u8;
}
```

### Configuration

Rising Sun will use a simple configuration approach:

```toml
[cdrom]
# Path to ISO file (optional)
iso = "/path/to/windows98.iso"

# Drive letter in guest (D: by default)
drive_letter = "D"
```

### UI Integration

The Qt6 frontend will provide:
- ISO file selection dialog
- Mount/unmount buttons
- Current ISO display
- "Eject" functionality (unmounts ISO)

## Differences: Original vs Rising Sun

| Feature | Original SunPCi | Rising Sun |
|---------|-----------------|------------|
| Physical CD-ROM | ✓ Pass-through | ✗ Not supported |
| Auto-mount | ✓ Via rmmount | ✗ Not supported |
| ISO mounting | ✗ Not supported | ✓ Primary feature |
| Audio CD | ✓ Play/pause/stop | ✗ Not supported |
| CD-RW write | ✓ Limited | ✗ Not supported |
| Volume manager | ✓ action_sunpci.so | ✗ Not needed |
| Boot from CD | ✓ El Torito | ✓ El Torito |

## Implementation Priority

### Phase 1: Basic ISO Support
1. ISO file mounting/unmounting
2. READ(10) command
3. READ CAPACITY
4. INQUIRY
5. TEST UNIT READY
6. REQUEST SENSE

### Phase 2: Guest OS Compatibility
1. MODE SENSE pages
2. READ TOC (for Windows compatibility)
3. Media change notification
4. Proper sense data responses

### Phase 3: Boot Support
1. El Torito boot catalog parsing
2. INT 13h CD-ROM extensions (AH=41h-4Fh)
3. Boot from ISO capability

## Dependencies

- ISO 9660 parsing library (or implement basic parser)
- SCSI command set knowledge
- INT 13h extension specification (El Torito)

## Open Questions

1. Do any guest OS drivers require specific SCSI device identification strings?
2. What MODE SENSE pages are required for Windows 95/98/NT CD detection?
3. Is Joliet extension support required for long filenames?
4. Should Rock Ridge extension be supported (for Linux guests)?

## References

- SCSI Commands Reference Manual (SPC-2)
- SCSI Multimedia Commands (MMC-2) 
- El Torito Bootable CD-ROM Format Specification
- ISO 9660 / ECMA-119 specification
