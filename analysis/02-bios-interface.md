# BIOS Interface Analysis

## Overview

The SunPCi card uses a custom Award BIOS stored in flash memory on the card. The BIOS provides standard PC BIOS services (INT 10h for video, INT 13h for disk, etc.) but hooks these interrupts to communicate with the Solaris host for actual I/O operations.

## BIOS File Structure

### File: `SUNWspci/bios/sunpci.bin`

| Property | Value |
|----------|-------|
| Size | 262,144 bytes (256 KB) |
| Format | LHa-compressed archive + raw BIOS |
| BIOS Vendor | Award Software, Inc. |
| BIOS Type | Award BootBlock BIOS v1.0 |
| SunPCi Version | .059 |
| Award ID | 2A5IIR09 |
| Copyright | 1998, Award Software, Inc. |

### BIOS Internal Structure

The 256KB file contains multiple components:

```
Offset      Content
------      -------
0x00000     LHa archive header (-lh5-)
            Contains: awardext.rom (9,744 bytes compressed)
            
0x36000     Award Decompression BIOS marker
            "= Award Decompression Bios ="
            
0x3E000     Award BootBlock BIOS v1.0
            Boot recovery code
            
0x3FF90     BIOS ID Area
            "SunPCi Bios Version .059"
            
0x3FFE0     Award BIOS ID: "2A5IIR09"
            
0x3FFF0     Reset vector and checksum area
            Jump to 0xE05B (boot entry point)
```

### Extracted Components

| Module | Compressed Size | Original Size | Ratio | Purpose |
|--------|-----------------|---------------|-------|---------|
| awardext.rom | ~7,120 bytes | 9,744 bytes | 73.1% | BIOS extension ROM |

The `awardext.rom` contains setup screens and PCI/ISA device enumeration:
- System configuration display
- CPU Type, Co-Processor, Clock
- Memory (Base, Extended, Cache)
- Drive detection (Diskette A, Hard Disk C/D)
- PCI device listing (Vendor ID, Device ID, IRQ)
- ISA/PNP device listing

### Supported Device Types (from BIOS strings)

```
Drive Types:
- Not Installed
- Installed
- None
- CDROM
- LS-120 (SuperDisk)
- ZIP-100
- MB (removable media)
```

> **Note:** The BIOS ROM contains standard Award BIOS UI elements that reference "UDMA Mode",
> but **no IDE/ATA controller is actually emulated**. These strings are artifacts of the 
> generic Award BIOS setup screens and are non-functional.

## Disk Access Architecture

The SunPCi card does **not** emulate an IDE controller. All disk I/O is virtualized through
the host-guest bridge:

| Guest OS | Disk Driver | Access Method |
|----------|-------------|---------------|
| DOS | BIOS INT 13h | RMIPC → Host driver → disk image |
| Windows 95/98 | `sunpci.vxd` | Bridge mailbox → Host driver |
| Windows NT/2000 | `emdisk.sys` | Presented as SCSI device via bridge |

### Windows NT Disk Stack

From `txtsetup.oem`, Windows NT sees the disk as a SCSI device:
- `bridge.sys` - PCI bridge driver (21554 non-transparent bridge)
- `emdisk.sys` - "SunPCi Disk Emulator" (Group: "System Bus Extender")

The `emdisk.sys` driver (source: `H:\src\penguin\sw\intel\winnt\disk\`) communicates with
the host through the bridge mailbox, not through any emulated disk controller hardware.

### Why No IDE Emulation?

1. **Simpler architecture** - No need to emulate complex IDE timing/DMA
2. **Better performance** - Direct bridge communication vs. port I/O emulation  
3. **Host-side flexibility** - Disk images handled entirely by host driver

The `SiS5598.vga` module in the BIOS is for **video only** (SiS 5597/5598 is a North Bridge
with integrated VGA graphics, not storage).

## INT 13h - Disk Services

### Library: `libint13.so.1.1`

The INT 13h handler is implemented in userspace, not in the BIOS ROM. The BIOS hooks INT 13h to call back to the Solaris host via the driver.

### Key Functions (from symbol table)

#### Dispatcher Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `NewInt13Dispatcher` | export | Create new INT 13h dispatcher |
| `InitDispatcher` | import | Register with core dispatcher |

#### Hard Disk Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `HardDiskRead` | 0x4d0c | Read sectors from hard disk |
| `HardDiskWrite` | 0x4e64 | Write sectors to hard disk |
| `HardDiskSeek` | 0x4b28 | Seek to cylinder/head |
| `HardDiskGetGeometry` | 0x4ad0 | Get CHS geometry |
| `HardDiskGetStatus` | 0x4b04 | Get drive status |
| `HardDiskFormat` | 0x4cd0 | Low-level format track |
| `HardDiskCalcOffset` | 0x4bc4 | CHS to LBA conversion |
| `HardDiskDestroy` | 0x5058 | Cleanup handler |

#### Floppy Disk Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `NewFloppyDisk` | 0x56d0 | Create floppy handler |
| `FloppyRead` | 0x54b0 | Read from floppy |
| `FloppyWrite` | 0x5508 | Write to floppy |
| `HwFloppyRead` | 0x601c | Hardware floppy read |
| `HwFloppyWrite` | 0x6270 | Hardware floppy write |
| `HwFloppyOpen` | export | Open floppy device |
| `HwFloppyFormat` | export | Format floppy |

#### CD-ROM Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `CDRead` | 0x6f20 | Read from CD-ROM |
| `CDWrite` | 0x6e30 | Write (for CD-RW) |
| `CdromEject` | export | Eject CD-ROM |
| `CDSendScsiCmd` | export | Send SCSI command |

#### INT 13h Standard Functions (Dispatch Table)
| Function | Offset | Purpose |
|----------|--------|---------|
| `DDiskRead` | 0x3970 | AH=02h: Read sectors |
| `DDiskWrite` | 0x3580 | AH=03h: Write sectors |
| `DStreamingDiskWrite` | 0x372c | Streaming write (SunPCi extension) |
| `DDiskDriveParams` | 0x33a4 | AH=08h: Get drive parameters |
| `DSetDiskParams` | 0x3d18 | AH=09h: Set drive parameters |
| `DDiskFormat` | 0x3b30 | AH=05h: Format track |
| `Int13Destroy` | 0x43e8 | Cleanup |
| `Int13DispatcherSwapReply` | 0x4330 | Byte-swap reply for SPARC |

#### Dispatch Tables
```c
// Three dispatch tables for different modes
NativeDispatchTable   @ 0x1c358  // SPARC native
DebugDispatchTable    @ 0x1c3f8  // Debug mode
bInt13Verbose         @ 0x1c354  // Verbose flag
```

### INT 13h Debug Messages

```c
"no disk for id %d: %x"
"libint13: DriveParams request. Drive: %d"
"ProcDiskWrite: Write >= 64k"
"ProcDiskWrite: sectors <= 0"
"Write Failed"
"libint13: DiskWrite() Drive %d Head: %d"
"Track: %d Sector: %d"
"Track: %x Cylinder: %d, Sector: %d"
"Sectors: %d"
"SunPCi: Streaming Writes supported on hard disk only"
"libint13: ProcStreamingDiskWrite: A fatal write error has occurred"
"libint13: ProcStreamingDiskWrite: Disk %d doesn't exist"
"ProcDiskRead: sectors <= 0"
```

### SunPCi Extension: Streaming Writes

The SunPCi BIOS adds a "Streaming Write" mode for better hard disk performance:
- Only supported on hard disks (not floppy/CD)
- Allows bulk writes without per-sector acknowledgment
- Reduces round-trips between x86 and SPARC

## INT 10h - Video Services

### Library: `libvga.so.1.1`

The VGA/video functions are implemented in userspace with X11 display.

### Key Functions (from symbol table)

#### Video Mode Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `NewVGADispatcher` | 0x3xx | Create VGA dispatcher |
| `NewVGATextMgr` | 0x37e4 | Create text mode manager |
| `VGATextMgrSetMode` | 0x2cf0 | Set video mode |
| `ProcSetVideoMode` | 0x45a8 | Process mode change |
| `SSetVideoMode` | 0x4628 | Server set mode |
| `DSetVideoMode` | 0x46e0 | Dispatch set mode |

#### Cursor Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `VGATextMgrSetCursor` | 0x29b8 | Set cursor shape |
| `VGATextMgrDrawCursor` | 0x2b98 | Draw cursor |
| `SetCursor` | export | Cursor control |
| `ProcSetCursor` | 0x4720 | Process cursor |
| `SSetCursor` | 0x4750 | Server cursor |
| `DSetCursor` | 0x47d0 | Dispatch cursor |

#### Graphics Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `VGATextMgrDraw8BitBitmap` | 0x1c44 | 8-bit bitmap drawing |
| `VGATextMgrDraw8BitBitmapAs24Bit` | 0x1df0 | 8→24 bit conversion |
| `VGATextMgrDraw4BitBitmap` | 0x1fe4 | 4-bit (16 color) |
| `VGATextMgrDraw4BitBitmapAs24Bit` | 0x21a4 | 4→24 bit conversion |
| `VGATextMgrDraw4BitPlanarBitmap` | 0x23c4 | Planar VGA mode |
| `VGATextMgrDraw4BitPlanarBitmapAs24Bit` | 0x2584 | Planar→24 bit |

#### Palette/DAC Functions
| Function | Offset | Purpose |
|----------|--------|---------|
| `VGATextMgrSetPalette` | export | Set VGA palette |
| `VGATextMgrSetDac` | 0x1a5c | Set DAC registers |
| `SetVGAPalette` | export | Palette control |

#### Register Access
| Function | Offset | Purpose |
|----------|--------|---------|
| `DDumpVideoRegs` | 0x3f00 | Dump VGA registers |
| `SetVideoData` | export | Write video data |
| `ProcSetVideoData` | 0x4108 | Process video data |

### Library: `libvideo.so.1.2`

Higher-level video library with X11 integration.

#### Surface Functions
| Function | Purpose |
|----------|---------|
| `BitmapSurfaceBitBlt` | Bit block transfer |
| `BitmapSurfaceDestroy` | Cleanup surface |
| `CreateDither` | Create dithering table |
| `DisplayBitmap` | Display bitmap to X |

#### Color Conversion
| Function | Purpose |
|----------|---------|
| `Convert1to1` | Mono conversion |
| `Convert4to8` | 16→256 color |
| `Convert4to24` | 16→TrueColor |
| `Convert8to8` | Palette remap |
| `Convert8to24` | 256→TrueColor |
| `Convert24to8` | TrueColor→256 |
| `Convert24to24` | Copy |

#### Dithering Tables
```c
dither16    // 16-color dither
dither16m   // 16-color dither (mode 2)
dither256   // 256-color dither
defaultWinColors256  // Default Windows palette
```

## Driver IOCTL Interface

### Device: `/dev/sunpcidrv2`

The Solaris driver exposes ioctls for communication.

### Known IOCTLs (from driver strings)

| IOCTL | Purpose |
|-------|---------|
| `SPCIO_GET_BOARDINFO` | Get board information |
| `SPCIO_GETREV` | Get revision |
| `SPCIO_SENDINTERRUPT` | Send interrupt to x86 |
| `SPCIO_SENDKBD` | Send keyboard data |
| `SPCIO_SENDMOUSE` | Send mouse data |
| `SPCIO_LOOPBACKPKT` | Loopback packet (debug) |
| `SPCIO_OSREV` | Get OS revision |
| `SPCIO_GETFH` | Get file handle |
| `SPCIO_HWATTACH` | Hardware attach |
| `SPCIO_FLASHMODE` | Enter flash mode |
| `SPCIO_FLASHRW` | Flash read/write |
| `SPCIO_RWPIC` | Read/write PIC |
| `SPCIO_USLEEP` | Microsleep |
| `SPCIO_GET_LED_STATE` | Get LED state |
| `SPCIO_GET_INSTANCE` | Get instance number |
| `SPCIO_REGEVENT` | Register event handler |
| `SPCIO_SENDESIG` | Send signal |
| `SPCIO_NETBIND` | Network bind |
| `SPCIO_MSAP` | Multi-SAP control |
| `SPCIO_MCAST` | Multicast table |
| `SPCIO_GIFADDR` | Get interface address |

### Driver Interrupt Handler

```c
sunpc_hwintr()  // Hardware interrupt handler
                // Handles: BIOS_RESET_RECEIVED
```

## Communication Protocol

### Protocol Header Format

```c
// Based on debug messages
struct DispatchHeader {
    uint32_t type;      // Dispatcher type (0x00-0xFF)
    uint32_t opcode;    // Operation code
    uint32_t length;    // Payload length
    // Payload follows...
};

// Protocol operations:
// - ALLOC_BUFFER of size 0x%x
// - SEND_BUFFER of size 0x%x  
// - DISPATCH_BUFFER header = 0x%x
```

### Dispatcher Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   CoreDispatcher                         │
│  ┌──────────┬──────────┬──────────┬──────────┐          │
│  │ INT13    │ VGA      │ Video    │ Others   │          │
│  │Dispatcher│Dispatcher│Dispatcher│...       │          │
│  └────┬─────┴────┬─────┴────┬─────┴────┬─────┘          │
│       │          │          │          │                 │
│       ▼          ▼          ▼          ▼                 │
│  ┌──────────────────────────────────────────┐           │
│  │         IOThread / Driver Interface      │           │
│  └────────────────────┬─────────────────────┘           │
│                       │                                  │
└───────────────────────┼──────────────────────────────────┘
                        │ ioctl()
                        ▼
              ┌─────────────────────┐
              │  /dev/sunpcidrv2    │
              │  (Kernel Driver)    │
              └─────────┬───────────┘
                        │
                        ▼
              ┌─────────────────────┐
              │   PCI Card (x86)    │
              │   Award BIOS        │
              └─────────────────────┘
```

## Rising Sun Implementation Strategy

### 1. BIOS Handling

For Rising Sun, we have two options:

**Option A: Use Original BIOS (Recommended for compatibility)**
- Extract and use the SunPCi BIOS from the card
- Implement host-side handlers for BIOS callbacks
- Maximum compatibility with original behavior

**Option B: Custom BIOS**
- Use SeaBIOS or coreboot
- Would require modifying callback interface
- More flexibility but less compatibility

### 2. INT 13h Implementation

```rust
// common/src/bios/int13.rs
pub enum Int13Function {
    Reset = 0x00,
    GetStatus = 0x01,
    ReadSectors = 0x02,
    WriteSectors = 0x03,
    Verify = 0x04,
    FormatTrack = 0x05,
    GetParams = 0x08,
    InitDrive = 0x09,
    ReadLong = 0x0A,
    WriteLong = 0x0B,
    Seek = 0x0C,
    ResetDisk = 0x0D,
    // Extended (INT 13h extensions)
    ExtendedRead = 0x42,
    ExtendedWrite = 0x43,
    ExtendedVerify = 0x44,
    GetDriveParams = 0x48,
}

pub struct Int13Request {
    pub function: u8,
    pub drive: u8,
    pub head: u8,
    pub cylinder: u16,
    pub sector: u8,
    pub count: u16,
    pub buffer_addr: u32,  // Linear address in x86 memory
}

pub trait DiskBackend {
    fn read(&mut self, lba: u64, buffer: &mut [u8]) -> Result<()>;
    fn write(&mut self, lba: u64, buffer: &[u8]) -> Result<()>;
    fn geometry(&self) -> DiskGeometry;
}
```

### 3. INT 10h Implementation

```rust
// common/src/bios/int10.rs
pub enum Int10Function {
    SetMode = 0x00,
    SetCursorShape = 0x01,
    SetCursorPos = 0x02,
    GetCursorPos = 0x03,
    ScrollUp = 0x06,
    ScrollDown = 0x07,
    ReadCharAttr = 0x08,
    WriteCharAttr = 0x09,
    WriteChar = 0x0A,
    SetPalette = 0x0B,
    WritePixel = 0x0C,
    ReadPixel = 0x0D,
    WriteTty = 0x0E,
    GetMode = 0x0F,
    // VGA functions
    SetDacRegs = 0x10,
    CharGenerator = 0x11,
    VideoConfig = 0x12,
    WriteString = 0x13,
    // VESA VBE
    VbeFunction = 0x4F,
}

pub struct VideoMode {
    pub mode: u8,
    pub width: u16,
    pub height: u16,
    pub bpp: u8,
    pub text_cols: u8,
    pub text_rows: u8,
}

pub trait VideoBackend {
    fn set_mode(&mut self, mode: u8) -> Result<()>;
    fn write_framebuffer(&mut self, offset: usize, data: &[u8]);
    fn read_framebuffer(&self, offset: usize, size: usize) -> Vec<u8>;
    fn current_mode(&self) -> VideoMode;
}
```

### 4. Linux Driver IOCTLs

```c
// driver/sunpci_ioctl.h
#define SUNPCI_IOCTL_MAGIC 'S'

#define SUNPCI_GET_BOARDINFO    _IOR(SUNPCI_IOCTL_MAGIC, 1, struct sunpci_boardinfo)
#define SUNPCI_GET_REVISION     _IOR(SUNPCI_IOCTL_MAGIC, 2, uint32_t)
#define SUNPCI_SEND_INTERRUPT   _IOW(SUNPCI_IOCTL_MAGIC, 3, struct sunpci_interrupt)
#define SUNPCI_FLASH_MODE       _IOW(SUNPCI_IOCTL_MAGIC, 4, int)
#define SUNPCI_FLASH_RW         _IOWR(SUNPCI_IOCTL_MAGIC, 5, struct sunpci_flash_op)
#define SUNPCI_REG_EVENT        _IOW(SUNPCI_IOCTL_MAGIC, 6, struct sunpci_event)
#define SUNPCI_DISPATCH         _IOWR(SUNPCI_IOCTL_MAGIC, 7, struct sunpci_dispatch)

struct sunpci_boardinfo {
    uint32_t board_revision;
    uint32_t bios_version;
    uint8_t  mac_address[6];
    uint32_t memory_size;
};

struct sunpci_dispatch {
    uint32_t type;
    uint32_t opcode;
    uint32_t length;
    uint8_t  data[];
};
```

## Files to Create

| Path | Purpose |
|------|---------|
| `common/src/bios/mod.rs` | BIOS module root |
| `common/src/bios/int13.rs` | INT 13h handler types |
| `common/src/bios/int10.rs` | INT 10h handler types |
| `daemon/src/handlers/int13.rs` | INT 13h implementation |
| `daemon/src/handlers/int10.rs` | INT 10h implementation |
| `driver/sunpci_bios.c` | BIOS interrupt routing |
| `driver/sunpci_ioctl.h` | IOCTL definitions |

## References

- [Award BIOS Structure](https://www.bioscentral.com/)
- [INT 13h Specification](https://en.wikipedia.org/wiki/INT_13H)
- [INT 10h Specification](https://en.wikipedia.org/wiki/INT_10H)
- [VGA Programming](https://wiki.osdev.org/VGA_Hardware)
- [Phoenix BIOS Decompression](http://www.uefi.org)
