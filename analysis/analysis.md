# SunPCI Reverse Engineering Analysis

## Overview

SunPCI was a hardware/software product from Sun Microsystems that allowed SPARC-based workstations running Solaris to run x86 PC operating systems (DOS, Windows 95/98/NT/2000). This document analyzes the original SUNWspci package (version 1.3) to understand its architecture for reimplementation in the Rising Sun project.

## Package Information

| Field | Value |
|-------|-------|
| Package Name | SUNWspci |
| Version | 1.3 |
| Architecture | SPARC |
| Target OS | Solaris 2.5.1+ |
| Install Location | /opt/SUNWspci |
| Copyright | Sun Microsystems, Inc. (1998-2000) |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         User Space                               │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────┐ │
│  │   sunpci     │  │ sunpcbinary  │  │  Shared Libraries      │ │
│  │ (shell script)│──►│ (main emulator)│◄──│ libvga, libvideo, etc │ │
│  └──────────────┘  └──────┬───────┘  └────────────────────────┘ │
│                           │                                      │
│                           │ ioctl                                │
│                           ▼                                      │
├─────────────────────────────────────────────────────────────────┤
│                       Kernel Space                               │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    sunpcidrv.xxx                          │   │
│  │              (Solaris kernel driver)                      │   │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                   SunPCI Hardware                         │   │
│  │          (PCI card with x86 CPU - pci108e,5043)          │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Component Analysis

### 1. Binaries (bin/)

| Binary | Type | Size | Purpose |
|--------|------|------|---------|
| `sunpci` | Shell script | 9.7 KB | Main launcher script |
| `sunpcbinary` | SPARC ELF | 441 KB | Core emulation binary |
| `makedisk` | SPARC ELF | 56 KB | Creates virtual disk images |
| `cphd` | SPARC ELF | 11 KB | Copy hard disk utility |
| `sunpciflash` | SPARC ELF | 25 KB | BIOS flash utility |
| `create_disk` | Korn shell | 5.5 KB | Disk creation wrapper |
| `vold_floppy_disable` | Shell script | 2.3 KB | Volume manager control |

#### sunpci (Launcher Script)

Key functionality:
- Validates Solaris version (requires 2.5.1+)
- Checks for driver installation (`/dev/sunpcidrv`)
- Verifies `/tmp` space (requires 1MB minimum)
- Verifies swap space (requires 12MB minimum)
- Auto-detects network interface (`NVL_INTERFACE`)
- Creates `~/pc` directory for user configuration
- Copies default `SunPC.ini` and fonts
- Compares BIOS versions (file vs card)
- Launches `sunpcbinary` with arguments

Command-line flags:
- `-h` : Help
- `-p` : Unknown (passed to sunpcbinary)
- `-f` : Unknown (consumed by script)
- `-c` : Unknown (passed to sunpcbinary)

#### sunpcbinary (Core Emulator)

The main emulation engine. SPARC 32-bit ELF binary (~441KB). This is the heart of the system that:
- Interfaces with the kernel driver
- Provides x86 CPU emulation (or interfaces with hardware x86 CPU on the PCI card)
- Manages display output (X11/OpenWindows)
- Handles input (keyboard/mouse translation)
- Coordinates with shared libraries for specific subsystems

### 2. Shared Libraries (lib/)

| Library | Size | Purpose |
|---------|------|---------|
| `libvideo.so.1.2` | 168 KB | Video/display emulation |
| `libvga.so.1.1` | 31 KB | VGA graphics mode support |
| `libint13.so.1.1` | 75 KB | INT 13h disk services (BIOS disk) |
| `libredir.so.1.0` | 57 KB | File system redirection |
| `libfsd95.so.1.1` | 44 KB | Windows 95 file system driver support |
| `libfsdnt.so.1.1` | 25 KB | Windows NT file system driver support |
| `libndis95.so.1.1` | 29 KB | Windows 95 NDIS network support |
| `libcopypaste.so.1.1` | 8 KB | Clipboard integration |
| `liblp.so.1.1` | 21 KB | Parallel port/printing support |
| `libperf.so.1.1` | 10 KB | Performance monitoring |
| `libsoftload.so.1.0` | 18 KB | Software loading utilities |
| `action_sunpci.so.1` | 12 KB | CD-ROM auto-mount action handler |

### 3. Kernel Driver (drivers/solaris/)

| File | Target OS | Size |
|------|-----------|------|
| `sunpcidrv.251` | Solaris 2.5.1 | 153 KB |
| `sunpcidrv.260` | Solaris 2.6 | 105 KB |
| `sunpcidrv.270` | Solaris 2.7 (32-bit) | 152 KB |
| `sunpcidrv.270.64` | Solaris 2.7 (64-bit) | 234 KB |
| `sunpcidrv.280` | Solaris 2.8 (32-bit) | 153 KB |
| `sunpcidrv.280.64` | Solaris 2.8 (64-bit) | 235 KB |
| `sunpcidrv.conf` | All | 78 bytes |
| `sunpcload` | All | 2.6 KB |

**PCI Device ID**: `pci108e,5043`
- Vendor: 108e (Sun Microsystems)
- Device: 5043 (SunPCI card)

**Device Node**: `/dev/sunpcidrv2`

The driver handles:
- PCI device enumeration and resource allocation
- Memory mapping between SPARC host and x86 card
- Interrupt routing
- Communication protocol with x86 subsystem

### 4. BIOS (bios/)

| File | Size | Purpose |
|------|------|---------|
| `sunpci.bin` | 256 KB | Custom BIOS for x86 subsystem |

The BIOS went through many revisions (.023 to .057+ documented). Key features:
- INT 13h floppy support
- SMI (System Management Interrupt) handling
- K6-2 CPU support
- Bridge buffer management (SPARC ↔ x86)
- Disk support up to 7.8GB (via geometry translation)
- Flash chip support (MXIC29F002T)

Notable BIOS bugs fixed over versions:
- Win95/98 installation hangs
- Win98 crashes
- NT setup hangs
- Office 2000 installation issues
- Partition Magic compatibility

### 5. Guest OS Drivers (drivers/)

#### Windows 95 (win95/)
- `sunpci.vxd` - Main SunPCI virtual device driver
- `spcdisp.drv/vxd` - Display driver
- `spcmouse.vxd` - Mouse driver
- `sunfsd.vxd` - File system driver (host access)
- `sunwndis.vxd` - Network driver (NDIS)
- `sunclip.exe` - Clipboard integration
- `sunnp.dll` / `sunpp.dll` - Print support
- `es1869.*` - ESS1869 sound card emulation
- `sis597.*` - SiS 597 video driver

#### Windows 98 (win98/)
Similar to Win95, with updated drivers:
- `sundisk.pdr` - Protected mode disk driver
- Updated display and mouse drivers

#### Windows NT/2000 (winnt/)
- `bridge.sys` - PCI bridge driver
- `emdisk.sys` - Emulated disk driver
- `sunfsd.sys` - File system driver
- `sunndis.sys` - Network driver
- `sunvideo.dll` / `sunvmini.sys` - Video driver
- `sermouse.sys` - Serial mouse driver
- `auddrive.sys` - Audio driver

### 6. DOS Support (defaults/)

The package includes Caldera OpenDOS 7.01:
- Complete DOS installation (`7.01/dos/`)
- `redir.sys` - Host file system redirector
- `sunpcnet.exe` - Network support
- `localize.exe` - Localization utility

Disk template: `C.7.01.template` (2.1 MB) - Pre-configured bootable disk image

### 7. Configuration Files

#### SunPC.ini
```ini
[Drives]
A drive=/dev/rdiskette
C drive=
CD=/vol/dev/aliases/cdrom0

[Disk32]
Enabled=Yes
```

#### Locale Support
Full i18n support for:
- English, German, French, Spanish, Italian, Swedish
- Japanese (multiple encodings)
- Korean
- Chinese (Simplified and Traditional)

## Key Technical Details

### Virtual Disk Format

- Magic number at offset 12: `0x53504349` ("SPCI")
- Variable geometry: 4, 8, or 16 heads
- Maximum size: 8GB (limited by BIOS geometry)
- Disk revision: 1 or 2

### Network Interface

Environment variable `NVL_INTERFACE` specifies the network interface for guest OS networking. Defaults to `lo0` for standalone machines.

### Display

Uses X11/OpenWindows for display. Resources loaded from:
- `$SUNPCIIHOME/lib/locale/$LANG/LC_MESSAGES/`
- Custom fonts: `pc8x16s.bdf`

### System Requirements (Original)

- Solaris 2.5.1 or later
- 12 MB swap space minimum
- 1 MB /tmp space minimum
- SunPCI hardware card

## Analysis Documents

1. **Virtual Disk Format** - Disk image structure → [Detailed Analysis](analysis/01-virtual-disk-format.md)
2. **BIOS Interface** - INT 13h, INT 10h handlers → [Detailed Analysis](analysis/02-bios-interface.md)
3. **Display Emulation** - VGA/SVGA graphics modes → [Detailed Analysis](analysis/03-display-emulation.md)
4. **Keyboard/Mouse** - Input translation tables (in `tables/keytables/`) → [Detailed Analysis](analysis/04-keyboard-mouse.md)
5. **File System Redirection** - Host ↔ Guest file sharing → [Detailed Analysis](analysis/05-filesystem-redirection.md)
6. **Clipboard** - Copy/paste between host and guest → [Detailed Analysis](analysis/06-clipboard.md)
7. **Network** - NDIS driver emulation → [Detailed Analysis](analysis/07-network.md)
8. **CD-ROM** - ISO file mounting → [Detailed Analysis](analysis/09-cdrom.md)
9. **Floppy Drive** - Floppy disk emulation → [Detailed Analysis](analysis/10-floppy.md)

## Comparison: Original vs Rising Sun

| Aspect | SunPCI (Original) | Rising Sun |
|--------|-------------------|------------|
| Host OS | Solaris/SPARC | Linux/x86-64 |
| x86 Execution | Hardware (PCI card) | Hardware (PCI card) |
| GUI Framework | X11/Motif | Qt6 |
| Driver Model | Solaris DDI/DKI | Linux kernel module |
| Language | C (presumed) | Rust + C |

## Files Requiring Further Analysis

1. `sunpcbinary` - Needs disassembly to understand x86 interface
2. `sunpcidrv.*` - Kernel driver binary analysis
3. `sunpci.bin` - BIOS disassembly for x86 emulation details
4. Shared libraries - Understanding plugin architecture

## Open Questions

1. How much x86 emulation was done in software vs hardware?
2. What was the communication protocol between host and x86 card?
3. How was video memory mapped between systems?
4. What was the RMIPC (Remote IPC) protocol mentioned in BIOS notes?
5. How did the bridge buffer binding work?
