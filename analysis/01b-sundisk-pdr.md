# Windows 9x 32-Bit Disk Driver (sundisk.pdr) Analysis

This document analyzes the Windows 95/98 32-bit disk driver (`sundisk.pdr`) as a reference for implementing Rising Sun's equivalent.

## Overview

Unlike DOS which uses INT 13h BIOS calls for every disk access, Windows 9x can use "32-bit disk access" via the IOS (I/O Supervisor) layer. SunPCi ships a port driver that bypasses BIOS entirely.

### Architecture Comparison

| Path | DOS/Real Mode | Win9x 32-bit |
|------|---------------|--------------|
| Layer | INT 13h → BIOS | IOS → Port Driver |
| Mode | Real Mode | Protected Mode |
| Context Switch | Per-sector V86 trap | None |
| Performance | Poor | Good |
| Driver | None (BIOS handles) | `sundisk.pdr` |

## Existing SunPCi Drivers

| File | Size | Platform | Description |
|------|------|----------|-------------|
| `sunpci.vxd` | 28KB | Win95 | IPC/bridge VxD (1999) |
| `sunpci.vxd` | 37KB | Win98 | IPC/bridge VxD (2000) |
| `sundisk.pdr` | 43KB | Win98 | 32-bit disk port driver |

**Key Finding**: `sundisk.pdr` was developed on Win95 path (`S:\penguin\sw\intel\win95\sundisk`) but only shipped with Win98. The Win95 `sunpci.vxd` has the identical IPC API, so the PDR should work on both.

## VxD IPC API (sunpci.vxd)

The port driver communicates with the host via services exported by `sunpci.vxd`:

### Protected Mode API Functions

| Function | Purpose |
|----------|---------|
| `SUNPCI_IpcWrite` | Write data to host (single buffer) |
| `SUNPCI_IpcWriteV` | Write data to host (scatter-gather) |
| `SUNPCI_IpcRead` | Read data from host (single buffer) |
| `SUNPCI_IpcReadV` | Read data from host (scatter-gather) |

### Internal Functions

| Function | Purpose |
|----------|---------|
| `PmApi_IpcWrite` | PM API handler for IpcWrite |
| `PmApi_IpcWriteV` | PM API handler for IpcWriteV |
| `PmApi_IpcRead` | PM API handler for IpcRead |
| `PmApi_IpcReadV` | PM API handler for IpcReadV |
| `PmApi_LoadLib` | Load shared library on host |
| `PmApi_Printf` | Debug printf to host |

## sundisk.pdr Architecture

### IOS Integration

The driver registers with Windows IOS as a port driver, handling:

| IOR Command | Handler | Description |
|-------------|---------|-------------|
| `IOR_READ` | `DiskRead()` | Read sectors |
| `IOR_WRITE` | `DiskWrite()` | Write sectors |
| `IOR_VERIFY` | (punt) | Verify sectors |
| `IOR_FORMAT` | (unsupported) | Format track |
| `IOR_SCSI_PASS_THROUGH` | (unsupported) | Raw SCSI |

### AEP (Async Event Packet) Handlers

| AEP | Handler | Description |
|-----|---------|-------------|
| `AEP_CONFIG_DCB` | `SetupDiskDCB()` | Configure disk DCB |
| `AEP_UNCONFIG_DCB` | | Remove disk DCB |
| `AEP_SYSTEM_REBOOT` | | Shutdown notification |
| `AEP_NO_INQ_DATA` | | SCSI inquiry failed |
| `AEP_NO_MORE_DEVICES` | | Enumeration complete |

### Device Types Supported

| Type | Setup Function | Inquiry String |
|------|----------------|----------------|
| Hard Disk | `SetupDiskDCB()` | "SunPCi Disk" |
| CD-ROM | `SetupCdDCB()` | "SunPCi CDRom" |
| Floppy | `SetupFloppyDCB()` | "SunPCi Floppy" |

## Protocol Analysis

The PDR uses the same IPC protocol as NT's `emdisk.sys` and BIOS INT 13h:

### Request Flow

```
IOS Request (IOR)
    ↓
sundisk.pdr (build IPC packet)
    ↓
SUNPCI_IpcWrite/IpcWriteV (via sunpci.vxd)
    ↓
PCI Bridge Scratchpad/Doorbell
    ↓
Host libint13.so (NewInt13Dispatcher)
    ↓
Disk Image File I/O
```

### Packet Format

Uses the same format documented in `01a-emdisk.md`:
- Byte 0: Drive type
- Byte 1: Command code (0x0a=read, 0x0b=write, 0x0c=params, etc.)
- Byte 2-3: Size encoding
- Byte 4: Drive number (0-4)

### Response Validation

```c
// Response opcode check (decompiled logic)
if (reply_opcode != expected_opcode) {
    debug_print("DiskRead invalid reply opcode");
    return error;
}
```

## Configuration

### INI Settings

```ini
[Disk32]
Enabled=Yes          ; Enable 32-bit disk access
StreamingWrite=Enabled  ; Enable streaming write optimization
Debug=0              ; Debug verbosity
```

### Registry (via INF)

```ini
HKR,,DevLoader,,*IOS
HKR,,DriverDesc,,"Sun 32 bit disk"
HKR,,PortDriver,,SUNDISK.PDR
```

## Implementation Strategy for Rising Sun

### Option A: Port the Win98 sundisk.pdr

**Pros:**
- Already implemented and tested
- Handles IOS edge cases
- Supports HDD, CD-ROM, and floppy

**Cons:**
- Binary-only, needs reverse engineering
- May have SunPCi-specific assumptions

### Option B: Write New PDR from Scratch

**Pros:**
- Clean implementation
- Can use modern tooling (Open Watcom, MASM)
- Full control over design

**Cons:**
- IOS API is complex and poorly documented
- Risk of subtle bugs

### Option C: Minimal VxD Hook

**Pros:**
- Simpler than full PDR
- Hook INT 13h in protected mode only

**Cons:**
- Less integrated with Windows
- May not get all performance benefits

## Recommended Approach

1. **Start with Win98 sundisk.pdr as reference** - it works
2. **Implement sunpci.vxd IPC API** first in our driver stack
3. **Test if existing sundisk.pdr works** with our IPC implementation
4. **If needed, write new PDR** using IOS documentation

## Key Files for Implementation

### Guest Side (Windows)
- `sundisk.pdr` - Reference 32-bit disk driver
- `sunpci.vxd` - IPC transport layer

### Host Side (Linux)  
- `libint13.so` - Already documented, same protocol
- `driver/src/storage.c` - Current INT 13h handler

## Open Questions

1. Can we use the existing `sundisk.pdr` binary with our implementation?
2. What VxD device ID does `sunpci.vxd` use for service dispatch?
3. Is the scatter-gather format compatible with Linux iovec?

## Related Documents

- [01a-emdisk.md](01a-emdisk.md) - NT disk driver protocol (same IPC format)
- [01-virtual-disk-format.md](01-virtual-disk-format.md) - Virtual disk image format
