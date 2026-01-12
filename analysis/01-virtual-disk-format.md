# SunPCi Virtual Disk Format Analysis

This document analyzes the virtual disk format used by SunPCi to understand how disk images are created, structured, and accessed.

## Overview

SunPCi uses a custom virtual disk format that wraps a standard DOS/MBR disk image with additional metadata. The format includes:

1. A SunPCi-specific header with magic number and geometry information
2. Standard DOS MBR and partition table
3. FAT file system with DOS boot files

## Magic Number

From `/etc/magic` entry added by postinstall:

```
12    long    0x53504349    SunPCi Disk Image Partitionable
```

- **Offset**: 12 bytes from start of file
- **Value**: `0x53504349` (ASCII: "SPCI")
- **Size**: 4 bytes (long)

**Note**: The template file `C.7.01.template` does NOT contain this magic number. It is added by `makedisk` when creating new disk images. The template is a raw DOS disk image used as source material.

## Disk Revisions

The `create_disk` script accepts a disk revision parameter:
```bash
create_disk new_disk_file size disk_revision
```

Supported revisions: 1 or 2 (based on script validation)

Strings found in makedisk:
- `get_disk_version`
- `sig_major_version`
- `sig_minor_version`
- `disk_revision`
- `Unsupported disk revision:`

## Header Structure (Inferred)

Based on string analysis of makedisk and libint13:

| Field | Purpose |
|-------|---------|
| `iheader_size` | Input header size |
| `oheader_size` | Output header size |
| `sig_major_version` | Major version number |
| `sig_minor_version` | Minor version number |
| `phys_heads` | Physical heads count |
| `phys_sectors` | Physical sectors per track |

## Geometry Handling

From makedisk strings and create_disk script:

### Size Limits
- Minimum: 10 MB (from create_disk)
- Maximum: 8000 MB (8 GB, from create_disk and BIOS notes)

### Cylinder Limit
- Maximum 1024 cylinders (CHS addressing limitation)
- `makedisk internal error: truncating number of cylinders to 1024`

### Variable Geometry
From create_disk comments:
> "This geometry uses either 4, 8, or 16 heads then calculates the cylinder value based on the size and number of heads."

Geometry selection algorithm (presumed):
| Disk Size | Heads | Rationale |
|-----------|-------|-----------|
| Small | 4 | Minimizes cylinder count |
| Medium | 8 | Balance |
| Large | 16 | Fits within 1024 cylinder limit |

### Sectors Per Track
Standard value: 17 or 63 (common CHS values)

## Template File Analysis

### File: `C.7.01.template`
- **Size**: 2,100,736 bytes (~2 MB)
- **Type**: DOS/MBR boot sector
- **DOS Version**: 3.3-7.0 compatible

### Partition Table (offset 0x1BE-0x1FD)
From hex dump at offset 0x1B0:
```
000001b0: 0000 0000 0000 0000 0000 0000 0000 8001  ................
000001c0: 0100 0103 113a 1100 0000 9b0f 0000 0000  .....:..........
```

Partition 1 entry (at 0x1BE):
- Status: `0x80` (active/bootable)
- CHS Start: (0, 1, 1)
- Type: `0x01` (FAT12)
- CHS End: (0x3A, 3, 17)
- LBA Start: 17 sectors
- Size: 3995 sectors

### Boot Sector Signature
- Offset 0x1FE: `55 AA` (valid MBR signature)

### FAT Volume Label
At end of disk:
```
00000200: 2507 0000 414d 5345 534c 4946 5641 5352  %...AMSESLIFVASR
00000210: 4f52 494d 4553 4145 50cd 0701 072d 0c00  ORIMESAEP....-..
```
Contains reversed strings like "AMSESLIFVASR" = "RSVAFILESMA" (possibly "SMAFILES" reversed)

## makedisk Command Line

From strings:
```
%s: %s -o <%s> -i <%s> -s <%s> -l -r <%s> -b -d <%s> [-c] [-n] [-f]
```

Options:
| Flag | Purpose |
|------|---------|
| `-o` | Output file name |
| `-i` | Input template file |
| `-s` | Size in MB |
| `-r` | Disk revision |
| `-l` | Label the disk |
| `-b` | Make bootable |
| `-d` | DOS directory to copy |
| `-c` | Copy OpenDOS files (for DOS 7.x+) |
| `-n` | Unknown |
| `-f` | Force/overwrite |

Actual invocation from create_disk:
```bash
$SUNPCIIHOME/bin/makedisk -o $disk_name -i $existing_disk -s $disk_size -r $disk_rev -f -l -b -d $DOS_DIR $ADD_ARGS
```

## Related Source Files

From debug strings embedded in binaries:
- `makedisk.c` - Main disk creation
- `libdisk.c` - Disk format library (shared with libint13)
- `utils.c` - Utility functions
- `ui.c` - User interface (X11 progress dialog)
- `progress.c` - Progress meter
- `Int13Dispatcher.c` - INT 13h handler dispatch
- `HardDisk.c` - Hard disk operations
- `Floppy.c` - Floppy disk operations
- `HwFloppy.c` - Hardware floppy access
- `cdrom.c` - CD-ROM operations

## Disk Validation Functions

Key functions identified:
- `ValidSigHeader` - Validate signature header
- `get_disk_version` - Read disk format version
- `get_disk_info` - Read disk information
- `get_bpb_info` - Read BIOS Parameter Block

Error messages provide validation points:
- `Could not read partition table on disk image!`
- `Could not read BPB`
- `Bad Signature %x`
- `Another Bad Signature %x`
- `Bad Indicator %x`

## Disk Default Location

From sunpcbinary strings:
- Default C drive: `~/pc/C.diskimage`
- Pattern matching: `[^.]*.diskimage` (non-hidden .diskimage files)

## INT 13h Operations

From libint13 strings, the library handles:

### Read/Write
- `DiskRead()` - Read sectors
- `DiskWrite()` - Write sectors
- `StreamingDiskWrite()` - Optimized writes

### Parameters logged
```
Drive %d Head: %d
Track: %d Sector: %d
Track: %x Cylinder: %d, Sector: %d
Sectors: %d
```

### Errors handled
- `Seek Error on Disk Image File`
- `unexpected read error. errno %d, nWanted %d nActual %d`
- `unexpected error. errno %d, nWanted %d nActual %d`

## Proposed Header Structure

Based on analysis (needs verification):

```c
// Offset 0: Start of file
struct sunpci_disk_header {
    uint8_t  boot_code[12];     // 0x00: MBR boot code fragment
    uint32_t magic;             // 0x0C: "SPCI" (0x53504349)
    uint8_t  major_version;     // 0x10: Major version
    uint8_t  minor_version;     // 0x11: Minor version
    uint16_t header_size;       // 0x12: Header size
    uint16_t heads;             // 0x14: Number of heads
    uint16_t sectors_per_track; // 0x16: Sectors per track
    uint32_t total_sectors;     // 0x18: Total sectors
    // ... more fields TBD
};
```

**WARNING**: This structure is speculative and needs verification through disassembly or testing.
