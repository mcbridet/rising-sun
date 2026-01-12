# Windows NT Disk Driver (emdisk.sys) IPC Protocol Analysis

This document analyzes the Windows NT/2000 disk driver (`emdisk.sys`) to understand how it communicates with the host for disk I/O operations, distinct from the BIOS INT 13h path.

## Overview

Unlike DOS and Windows 9x which use INT 13h BIOS calls for disk access, Windows NT/2000 uses a native kernel-mode disk driver (`emdisk.sys`) that communicates directly with the host via the bridge.sys IPC API.

## Driver Components

| Driver | Purpose |
|--------|---------|
| `bridge.sys` | PCI bridge driver, provides IPC channel API |
| `emdisk.sys` | Emulated disk driver (HDD, floppy, CD-ROM) |
| `sunfsd.sys` | Filesystem redirection driver |
| `sunndis.sys` | Network driver |

## bridge.sys Exported API

The bridge driver exports 22 functions for IPC channel management:

| Function | Purpose |
|----------|---------|
| `SunPCiIpcCreateChannel@24` | Create named IPC channel (6 params) |
| `SunPCiIpcDeleteChannel@8` | Delete IPC channel (2 params) |
| `SunPCiIpcBindChannel@4` | Bind to existing channel |
| `SunPCiIpcBindChannelForceUnBind@4` | Force unbind and rebind |
| `SunPCiIpcUnBindChannel@4` | Unbind from channel |
| `SunPCiIpcBuildIoControlIrp@36` | Build disk I/O control IRP (9 params) |
| `SunPCiIpcBuildReadIrp@28` | Build read IRP (7 params) |
| `SunPCiIpcBuildWriteIrp@28` | Build write IRP (7 params) |
| `SunPCiIpcTransfer@20` | Simple data transfer (5 params) |
| `SunPCiIpcSGTransfer@28` | Scatter-gather transfer (7 params) |
| `SunPCiIpcPagedTransfer@20` | Paged memory transfer (5 params) |
| `SunPCiIpcPagedSGTransfer@28` | Paged scatter-gather (7 params) |
| `SunPCiIpcGetRequestBlockSize@0` | Get request buffer size |
| `SunPCiIpcGetReplyBlockSize@0` | Get reply buffer size |
| `SunPCiIpcGetVersion@0` | Get IPC protocol version |
| `SunPCiIpcGetLastVideoValues@16` | Video-related state (4 params) |
| `SunPCiIpcMouseCoordUpdate@16` | Mouse coordinate update |
| `SunPCiIpcRegisterEventCallback@12` | Register event handler (3 params) |
| `SunPCiIpcCoreChannel` | (data) Core channel handle |
| `SunPCiIpcPrintf` | Debug printf function |
| `SpcGetNamedProfileEntry` | Get profile configuration |
| `BridgeDebugLevel` | (data) Debug verbosity level |

## Device Structure Layout

Each disk device maintains state at fixed offsets within its device extension:

| Offset | Size | Purpose |
|--------|------|---------|
| 0x210 | 4 | IPC channel handle |
| 0x214 | 4 | Secondary handle/parameter |
| 0x218-0x220 | varies | Geometry/size info |
| 0x25c | 1 | Drive type identifier |
| 0x25d | 1 | Drive sub-type/flags |
| 0x26c-0x2c0 | varies | Scatter-gather list pointers |
| 0x2b0 | 4 | SG list count |
| 0x2b4-0x2c0 | varies | SG buffer pointers |
| 0x2c4 | 4 | Request size |
| 0x2c8 | 4 | Response size |
| 0x2d0 | 4 | Status/result |
| 0x2d4-0x2dc | 8+ | Command packet buffer |

## Disk Command Packet Format (Request)

The NT driver uses a different packet format than the BIOS INT 13h path. Commands are built at offset 0x2d4 in the device extension:

### NT Driver Request Header

```c
struct emdisk_request {
    uint8_t  drive_type;    // Byte 0: drive type from offset 0x25c
    uint8_t  command;       // Byte 1: command code (0x0a, 0x0c, etc.)
    uint8_t  size_hi;       // Byte 2: ((response_size + 3) >> 10) & 0xFF
    uint8_t  size_lo;       // Byte 3: ((response_size + 3) >> 2) & 0xFF
    uint8_t  drive_num;     // Byte 4: drive number (0-4)
    // Additional fields vary by command type
};
```

### For SCSI Commands (0x0f)

```c
struct emdisk_scsi_request {
    // Header (bytes 0-4) as above
    uint8_t  cdb_length;    // Byte 5: length of SCSI CDB
    uint8_t  reserved[2];   // Bytes 6-7: reserved
    uint32_t xfer_in_len;   // Bytes 8-11: data-in transfer length
    uint32_t xfer_out_len;  // Bytes 12-15: data-out transfer length
    uint8_t  cdb[16];       // Bytes 16+: SCSI CDB
    // Write data follows for WRITE commands
};
```

### Drive Number Mapping (Byte 4)

| Value | Drive Type |
|-------|------------|
| 0-1   | Floppy (A:, B:) |
| 2-3   | Hard disk (C:, D:) |
| 4     | CD-ROM |

## Response Packet Format

The host returns a response with the following format:

```c
struct int13_response {
    uint8_t  command;       // Byte 0: Echoed command from request
    uint8_t  response_type; // Byte 1: Response type code (see below)
    uint8_t  size_hi;       // Byte 2: Payload size (32-bit words, high byte)
    uint8_t  size_lo;       // Byte 3: Payload size (32-bit words, low byte)
    uint8_t  error_code;    // Byte 4: INT 13h error status (AH return)
    uint8_t  error_detail;  // Byte 5: Error detail (0xAA = invalid disk)
    uint8_t  count;         // Byte 6: Actual sectors transferred
    uint8_t  reserved;      // Byte 7: Reserved
    // Sector data follows for read operations
};
```

### Response Type Codes (Byte 1)

| Code | Meaning | Function |
|------|---------|----------|
| 0x97 | Disk read success | Read sectors completed |
| 0x99 | Get drive params | Response to INT 13h AH=08h |
| 0x9c | SCSI command | SCSI pass-through response |
| 0x9d | Drive info | Extended drive information |
| 0x9e | Media info | Media status/change |
| 0x9f | Error | Operation failed |

### Size Calculation

The size field contains the payload size in 32-bit words:
```c
size_words = (sector_count * 512 + 11) / 4;
size_hi = (size_words >> 8) & 0xFF;
size_lo = size_words & 0xFF;
```

## IOCTL Code Analysis

emdisk.sys uses two IOCTL codes when calling `SunPCiIpcBuildIoControlIrp`:

### IOCTL 0x9c41e484 (Main Disk I/O)

```
Device Type: 0x9c41 (40001) - Custom SunPCi device
Access: 3 (FILE_READ_ACCESS | FILE_WRITE_ACCESS)
Function: 0x921 (2337) - Disk I/O with scatter-gather
Method: 0 (METHOD_BUFFERED)
```

Used for read/write operations with scatter-gather support.

### IOCTL 0x9c41e480 (Simple Disk I/O)

```
Device Type: 0x9c41 (40001) - Custom SunPCi device
Access: 3 (FILE_READ_ACCESS | FILE_WRITE_ACCESS)  
Function: 0x920 (2336) - Simple disk I/O
Method: 0 (METHOD_BUFFERED)
```

Used for simpler operations without scatter-gather (SCSI ioctls).

### SCSI Command Dispatch (ESendScsiIoctl)

The SCSI ioctl handler uses these command codes:

| Cmd | Response | Description |
|-----|----------|-------------|
| 0x0a | 0x97 | Disk read/write sectors |
| 0x0c | 0x99 | Get drive parameters |
| 0x0f | 0x9c | SCSI command |
| 0x10 | 0x9d | Extended info |
| 0x11 | 0x9e | Media info |

## Channel Creation

**Critical Finding:** emdisk.sys creates a channel named **"NewInt13Dispatcher"** - the same dispatcher name used by the BIOS INT 13h path!

This means:
- Both NT and BIOS disk access go through the same host-side dispatcher
- The host's `libint13.so` library handles both paths
- No separate dispatcher ID is needed for NT

Channel name found at offset 0x10680 in emdisk.sys as UTF-16LE:
```
"NewInt13Dispatcher"
```

```c
// Pseudo-code from disassembly
NTSTATUS CreateDiskDevice(...)
{
    UNICODE_STRING channelName;
    
    // Channel name is "NewInt13Dispatcher" at 0x10680
    RtlInitUnicodeString(&channelName, L"NewInt13Dispatcher");
    
    // Create channel with 6 parameters
    status = SunPCiIpcCreateChannel(
        &channelName,           // "NewInt13Dispatcher"
        &deviceExtension,       // Device context
        &outputHandle,          // Output channel handle
        flags,                  // Creation flags
        &driverObject,          // Driver reference
        &deviceObject           // Device reference
    );
    
    // Store channel handle at offset 0x210
    deviceExtension->ChannelHandle = outputHandle;
}
```

## Disk I/O Flow

### issue_diskio Function (0x1200a)

The main disk I/O function:

1. Build command packet at offset 0x2d4
2. Set drive type from 0x25c
3. Calculate size fields (size >> 10, size >> 2)
4. Build scatter-gather lists if needed
5. Call `SunPCiIpcBuildIoControlIrp` with:
   - Channel handle (from 0x210)
   - IOCTL code 0x9c41e484
   - Reply buffer size (0x18 = 24 bytes)
   - Scatter-gather list pointers
6. Wait for completion
7. Process response

### start_diskio Function (0x126db)

Higher-level wrapper that:

1. Validates parameters
2. Calculates CHS from LBA if needed
3. Retries on failure (up to 4 times)
4. Handles sector alignment and padding
5. Calls issue_diskio

## Operation Codes

From string analysis and code patterns:

### NT Driver Uses SCSI Commands

**Important:** Unlike the BIOS path (which uses INT 13h function codes 02h/03h), the NT driver uses SCSI commands:

| SCSI Opcode | Description | Purpose |
|-------------|-------------|---------|
| 0x28 | READ(10) | Read sectors |
| 0x2A | WRITE(10) | Write sectors |
| 0x00 | TEST UNIT READY | Media check |
| 0x12 | INQUIRY | Get device info |
| 0x1A | MODE SENSE(6) | Get device parameters |
| 0x25 | READ CAPACITY | Get disk size |

These SCSI commands are wrapped in IPC packets and sent through the "NewInt13Dispatcher" channel, where the host's `ProcSendScsiCmd` function handles them.

## CD-ROM Specific Commands

CD-ROM uses SCSI-style commands through the same IPC channel:

- `EReadTOC` - Read table of contents
- `EPlayAudio` - Audio playback
- `ESeekAudio` - Seek to position
- `EStopAudio` - Stop playback
- `ECheckVerify` - Media check
- `EGetDriveGeometry` - Get capacity
- `EReadQChannel` - Read Q subchannel

## Error Handling

Debug messages reveal error conditions:

```
"DiskIo: Returned reply (%x) not expected one (%x)"
"Invalid function %x in start_diskio"
"Invalid opcode %d in SynchDiskCommand"
"Could not create channel, status %x"
```

## Source File References

From embedded debug strings:

```
H:\src\penguin\sw\intel\winnt\disk\disk.c
H:\src\penguin\sw\intel\winnt\bridge\bridge.c
```

## Comparison: INT 13h vs NT Driver

| Aspect | INT 13h (BIOS) | emdisk.sys (NT) |
|--------|----------------|-----------------|
| Entry Point | BIOS trap | Native IPC channel |
| Channel Name | "NewInt13Dispatcher" | "NewInt13Dispatcher" (SAME!) |
| Command Type | INT 13h AH codes (02, 03, 08) | SCSI opcodes (28, 2A, 25) |
| Drive ID | 0x00, 0x80, 0xE0 | 0-4 mapped (floppy/HD/CD) |
| Data Transfer | Ring buffer payload | Scatter-gather lists |
| IOCTL | N/A | 0x9c41e484 / 0x9c41e480 |
| Host Handler | ProcDiskRead/Write | ProcSendScsiCmd |
| Host Library | libint13.so.1.1 | libint13.so.1.1 (SAME!) |

**Key Insight:** Both NT and BIOS use the same "NewInt13Dispatcher" channel and same host library, but:
- **BIOS path**: Uses INT 13h function codes (AH=02h for read, 03h for write)
- **NT path**: Uses SCSI CDBs (opcode 28h for READ(10), 2Ah for WRITE(10))

The host library has separate handlers for both:
- `ProcDiskRead`/`ProcDiskWrite` - for INT 13h-style commands
- `ProcSendScsiCmd` - for SCSI CDB passthrough

## Implementation Notes for Rising Sun

### Current State

The existing `storage.c` handler supports INT 13h BIOS requests via the STORAGE dispatcher (ID 8). This works for DOS and Windows 9x.

### Required for NT Support

1. **Named Channel Support**: Handle "NewInt13Dispatcher" channel creation from bridge.sys
2. **SCSI CDB Processing**: Add SCSI opcode handlers:
   - 0x28 READ(10): Read sectors using LBA
   - 0x2A WRITE(10): Write sectors using LBA
   - 0x25 READ CAPACITY: Return disk size
   - 0x00 TEST UNIT READY: Return ready status
   - 0x12 INQUIRY: Return device identification
   - 0x1A MODE SENSE: Return device parameters
3. **IOCTL Processing**: Recognize IOCTL 0x9c41e484 and 0x9c41e480
4. **Drive Mapping**: Map drive numbers 0-4 to configured disk images:
   - 0-1: Floppy drives
   - 2-3: Hard disks
   - 4: CD-ROM
5. **Scatter-Gather**: Support SG list transfers for large I/O
6. **Byte Order**: Handle byte swapping for SPARC↔x86

### Architecture Notes

```
┌────────────────────────────────────────────────────────────────┐
│                     Windows NT Guest                           │
├────────────────────────────────────────────────────────────────┤
│  emdisk.sys                                                    │
│  ├── Build SCSI READ(10)/WRITE(10) CDBs                        │
│  ├── Wrap in IPC packet (drive_type, cmd, size, cdb)           │
│  └── Call SunPCiIpcBuildIoControlIrp with IOCTL 0x9c41e484     │
├────────────────────────────────────────────────────────────────┤
│  bridge.sys                                                    │
│  ├── SunPCiIpcCreateChannel("NewInt13Dispatcher")              │
│  └── SunPCiIpcSGTransfer (scatter-gather)                      │
├────────────────────────────────────────────────────────────────┤
│                   Intel 21554 Bridge                           │
├────────────────────────────────────────────────────────────────┤
│  Host Driver (Rising Sun)                                      │
│  ├── Ring buffer receive                                       │
│  ├── Dispatch to storage handler                               │
│  └── Process SCSI CDB or INT 13h command                       │
├────────────────────────────────────────────────────────────────┤
│  Storage Backend                                               │
│  ├── Virtual disk images (.diskimage)                          │
│  ├── Floppy images (.img)                                      │
│  └── ISO images (.iso)                                         │
└────────────────────────────────────────────────────────────────┘
```

### Dispatcher Routing

The NT disk driver uses a **named channel** approach:
- Channel name: "NewInt13Dispatcher"
- This is NOT routed through the numeric dispatcher ID system
- The bridge.sys `SunPCiIpcCreateChannel` creates a direct path

## Open Questions

1. ~~What dispatcher ID does emdisk.sys channel use?~~ **RESOLVED: Uses "NewInt13Dispatcher" channel name**
2. ~~Exact format of the 24-byte response block?~~ **RESOLVED: See Response Packet Format**
3. ~~How are multiple drives differentiated on the host side?~~ **RESOLVED: See Drive Number Routing**
4. ~~Is there a handshake/initialization sequence?~~ **RESOLVED: See Initialization Sequence**
5. ~~How does the host know which disk image to use?~~ **RESOLVED: See Configuration and Virtual Disk Format**

---

## Drive Number Routing (Q3 Resolution)

Multiple drives are differentiated via **byte 4** of every request packet (the drive number):

| Drive Number | Device Type | INI Key | BIOS Drive |
|--------------|-------------|---------|------------|
| 0 | Floppy A: | `A drive` | 0x00 |
| 1 | Floppy B: | `B drive` | 0x01 |
| 2 | Hard Disk C: | `C drive` | 0x80 |
| 3 | Hard Disk D: | `D drive` | 0x81 |
| 4 | CD-ROM | `CD` | N/A |

The host-side `libint13.so` dispatcher maintains an array of drive context structures. Each request's byte 4 indexes into this array to select the correct disk image file descriptor, geometry info, and function dispatch table.

---

## Initialization Sequence (Q4 Resolution)

The initialization happens entirely on the **host side** when `sunpci` starts:

### Host-Side Initialization Flow

1. **Parse SunPC.ini**: Read `[Drives]` section for disk image paths
2. **Create Dispatcher**: `NewInt13Dispatcher()` allocates a 0x98 byte dispatcher context
3. **Register Channel**: `InitDispatcher()` registers "NewInt13Dispatcher" as the IPC channel name
4. **Create Drive Contexts**: For each configured drive:
   - Floppy: `NewFloppyDisk(path, drive_num, context)`
   - HDD: `NewHardDisk(path, drive_num, context)` 
   - CD-ROM: `NewCDRom(device_path, context)`
5. **Setup Function Tables**: `CopyFuncs()` installs dispatch tables (Proc*, S*, D* variants)

### Guest-Side Initialization (emdisk.sys)

1. Driver entry calls `SunPCiIpcCreateChannel("NewInt13Dispatcher", ...)`
2. If channel doesn't exist yet, returns error (host must start first - "No soup for you!")
3. Creates device objects for each drive type
4. Each device sends `INQUIRY` (SCSI 0x12) or `Get Parameters` to enumerate

There is **no handshake protocol**. The host creates the channel and waits; the guest connects when the driver loads.

---

## Configuration and Virtual Disk Format (Q5 Resolution)

### SunPC.ini Configuration File

Located at `~/pc/SunPC.ini` (user) or `$SUNPCIIHOME/defaults/SunPC.ini` (template):

```ini
[Drives]
A drive=/dev/rdiskette     # Physical floppy or image path
B drive=                    # Optional second floppy
C drive=/home/user/pc/C.vdisk  # Primary hard disk image
D drive=                    # Optional secondary hard disk
CD=/vol/dev/aliases/cdrom0  # CD-ROM device

[Disk32]
Enabled=Yes                 # 32-bit disk access enabled
```

### Virtual Disk Image Header Format

SunPCi virtual disk images have a 1024-byte (0x400) header prepended to the raw disk data:

```c
struct sunpci_disk_header {
    uint8_t  reserved1[8];      // Offset 0x00: Reserved
    uint32_t header_size;       // Offset 0x08: Header size (must be 0x400)
    uint32_t magic;             // Offset 0x0C: "SPCI" = 0x53504349
    uint32_t heads;             // Offset 0x10: Number of heads
    uint32_t cylinders;         // Offset 0x14: Number of cylinders
    uint32_t sectors;           // Offset 0x18: Sectors per track
    // ... remaining header fields up to 0x400 ...
};
```

### Header Validation (`ValidSigHeader`)

The `get_disk_info()` function validates disk images:

1. Read first 0x400 bytes from file
2. Check `header[0x0C] == 0x53504349` ("SPCI" magic)
3. Check `header[0x08] == 0x400` (header size)
4. If valid: Use `new_get_disk_info()` to extract CHS from header
5. If invalid: Use `old_get_disk_info()` to parse MBR partition table at offset 0x1BE

### Data Offset

All disk I/O offsets are adjusted by the header size:
- **New format**: Data starts at byte 0x400 (after header)
- **Old format**: Data starts at byte 0 (raw disk image, no header)

### Creating Virtual Disks

The `makedisk` utility creates properly formatted disk images:
```bash
makedisk -o output.vdisk -i template.img -s 500 -r 1.0
```
This writes the SPCI header with geometry calculated from the size.

## Host-Side Library: libint13.so.1.1

The host-side INT 13h handler is implemented in `libint13.so.1.1`. Key components:

### Exported Functions

| Address | Symbol | Purpose |
|---------|--------|---------|
| 0x4478 | `NewInt13Dispatcher` | Main dispatcher entry point |
| 0x43e8 | `Int13Destroy` | Cleanup dispatcher |
| 0x37cc | `ProcDiskRead` | Process disk read request |
| 0x33f8 | `ProcDiskWrite` | Process disk write request |
| 0x3184 | `ProcDiskDriveParams` | Process get parameters (AH=08h) |
| 0x3f30 | `ProcSendScsiCmd` | SCSI pass-through |
| 0x41e4 | `ProcScsiIoctlCmd` | SCSI ioctl handling |
| 0x3be4 | `ProcSetDiskParams` | Set disk parameters |
| 0x3dd4 | `ProcFloppyRecal` | Floppy recalibrate |

### Dispatch Tables

Three dispatch tables at runtime (80 bytes each = 20 entries × 4 bytes):

| Address | Table | Purpose |
|---------|-------|---------|
| 0x1c358 | NativeDispatchTable | Native byte order dispatch |
| 0x1c3a8 | SwappedDispatchTable | Byte-swapped dispatch (x86↔SPARC) |
| 0x1c3f8 | DebugDispatchTable | Debug/verbose dispatch |

### Function Pattern

Each command has three implementations:
- `Proc*` - Process/validate level (e.g., `ProcDiskRead`)
- `S*` - Swapped byte order (e.g., `SDiskRead`)
- `D*` - Direct implementation (e.g., `DDiskRead`)

### SCSI Support

Full SCSI-2/MMC command set supported for CD-ROM:

| Command | Description |
|---------|-------------|
| 0x00 | Test Unit Ready |
| 0x03 | Request Sense |
| 0x12 | Inquiry |
| 0x1A | Mode Sense (6) |
| 0x1B | Start/Stop Unit |
| 0x1E | Prevent/Allow Medium Removal |
| 0x25 | Read Capacity |
| 0x28 | Read (10) |
| 0x42 | Read Sub-Channel |
| 0x43 | Read TOC |
| 0x47 | Play Audio MSF |
| 0x4B | Pause/Resume |
| 0xBE | Read CD |

### Source Files (from strings)

```
Int13Dispatcher.c    - Main dispatcher logic
HardDisk.c          - Hard disk handlers
Floppy.c            - Floppy handlers
cdrom.c             - CD-ROM SCSI handlers
```

## Device Names Created by emdisk.sys

The NT driver creates standard Windows NT device objects:

| Pattern | Description |
|---------|-------------|
| `\Device\Harddisk%d\Partition%d` | Standard NT disk partition |
| `\DosDevices\PhysicalDrive%d` | DOS device symlink |
| `\ArcName\multi(0)disk(0)fdisk(%d)` | ARC name for boot loader |
| `\Device\MediaChangeEvent%d` | Media change notification event |
| `\Registry\Machine\Hardware\Description\System\MultifunctionAdapter\2\DiskController\0\FloppyDiskPeripheral\0` | Floppy registry key |

The driver identifies itself as:
- Description: "SunPCi Emulated Disk Driver"
- Short name: "EmDisk"  
- File: "emdisk.sys"

## Related Files

- `driver/src/storage.c` - Current INT 13h handler
- `driver/src/ipc.h` - IPC protocol definitions
- `analysis/01-virtual-disk-format.md` - Disk image format
