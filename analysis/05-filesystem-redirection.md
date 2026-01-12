# File System Redirection Analysis

## Overview

The SunPCI file system redirection (FSD) subsystem allows the guest operating system to access files on the Solaris host transparently. This creates virtual network drives that map host directories to DOS/Windows drive letters in the guest.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Guest (x86)                                     │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    Guest OS File System Layer                       │ │
│  ├────────────────┬─────────────────────┬─────────────────────────────┤ │
│  │     DOS        │    Windows 95/98    │     Windows NT/2000          │ │
│  │  ┌──────────┐  │  ┌───────────────┐  │  ┌─────────────────────┐    │ │
│  │  │redir.sys │  │  │  sunfsd.vxd   │  │  │    sunfsd.sys       │    │ │
│  │  │          │  │  │  (IFS Hook)   │  │  │ (Kernel FS Driver)  │    │ │
│  │  └────┬─────┘  │  └───────┬───────┘  │  └──────────┬──────────┘    │ │
│  │       │        │          │          │             │                │ │
│  │  ┌────┴─────┐  │          │          │             │                │ │
│  │  │sunpcnet  │  │          │          │             │                │ │
│  │  │.exe      │  │          │          │             │                │ │
│  │  └──────────┘  │          │          │             │                │ │
│  └────────────────┴──────────┼──────────┴─────────────┼────────────────┘ │
│                              │                        │                  │
│                              ▼                        ▼                  │
│                    ┌─────────────────────────────────────┐               │
│                    │        Host Communication           │               │
│                    │    (RMIPC / Driver Interface)       │               │
│                    └────────────────┬────────────────────┘               │
└─────────────────────────────────────┼────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Host (SPARC/Solaris)                             │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                       sunpcbinary                                 │   │
│  │                                                                   │   │
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────────────┐  │   │
│  │  │ libredir.so    │  │ libfsd95.so    │  │ libfsdnt.so        │  │   │
│  │  │ (DOS Redir)    │  │ (Win95 FSD)    │  │ (WinNT FSD)        │  │   │
│  │  └───────┬────────┘  └───────┬────────┘  └───────┬────────────┘  │   │
│  │          │                   │                   │                │   │
│  │          └───────────────────┼───────────────────┘                │   │
│  │                              │                                    │   │
│  │                              ▼                                    │   │
│  │                    ┌─────────────────┐                            │   │
│  │                    │    InitDispatcher (Plugin Registration)      │   │
│  │                    └────────────────────────────────────────────┘ │   │
│  │                              │                                    │   │
│  │                              ▼                                    │   │
│  │                    ┌─────────────────┐                            │   │
│  │                    │  Solaris VFS    │                            │   │
│  │                    │  (Host Files)   │                            │   │
│  │                    └─────────────────┘                            │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Analysis

### 1. DOS Redirector (redir.sys + sunpcnet.exe)

#### redir.sys - DOS Device Driver

| Property | Value |
|----------|-------|
| Type | DOS Block Device Driver |
| Location | `defaults/7.01/sunpc/redir.sys` |
| Size | ~10 KB |
| Magic | `HFX` (Host File eXchange) |
| Version | 1.1 |
| Host Library | `libredir.so.1.0` |

**Header Structure (offset 0x00):**
```
Offset  Size  Content
------  ----  -------
0x00    2     Link to next driver (0xFFFF = end)
0x02    2     Driver attributes
0x04    2     Strategy entry point offset
0x06    2     Interrupt entry point offset
0x08    8     Driver name "HFX     " (padded)
0x10    2     "UU" marker
```

**Functionality:**
- Implements DOS Network Redirector interface (INT 2Fh AH=11h)
- Intercepts file operations on virtual network drives
- Communicates with host via `libredir.so.1.0` dispatcher

#### sunpcnet.exe - Drive Mapping Utility

| Property | Value |
|----------|-------|
| Type | DOS Executable |
| Location | `defaults/7.01/sunpc/sunpcnet.exe` |
| Purpose | Map/unmap host directories to drive letters |

**Command Syntax:**
```
sunpcnet use                           # Display all mappings
sunpcnet use X: /unix/path [/ms]       # Map path to drive
sunpcnet use X: /d                     # Delete mapping
```

**Special Path Variables:**
| Variable | Meaning |
|----------|---------|
| `$SUNPCIIHOME` | SunPCI installation directory |
| `/` | Root filesystem |
| `~` | User's home directory |

**Default Mappings (from autoexec.bat):**
```batch
sunpcnet use f: $SUNPCIIHOME    # F: = /opt/SUNWspci
sunpcnet use r: /                # R: = / (root)
sunpcnet use h: ~                # H: = $HOME
```

**Flags:**
| Flag | Purpose |
|------|---------|
| `/ms` | Enable MS-DOS compatible sharing mode |
| `/ml` | Enable mandatory locking (requires `/ms`) |
| `/d` | Delete/disconnect drive mapping |

### 2. Windows 95/98 FSD (sunfsd.vxd)

| Property | Value |
|----------|-------|
| Type | Windows VxD (Virtual Device Driver) |
| Location | `drivers/win95/sunfsd.vxd` |
| INF File | `sunfsd.inf` |
| Device Class | HDC (Hard Disk Controller) |
| Host Library | `libfsd95.so.1.1` |

**VxD Registration:**
- Registered as Installable File System (IFS) driver
- Uses `IFSMgr_NetFunction` hook for network drive operations
- Provider registered via `IFSMgr_RegisterNet`

**Configuration File:**
```
C:\drives.map    # Drive mapping configuration (read at boot)
```

**Supported Operations:**

| VFS Operation | Description |
|---------------|-------------|
| `VF_FileAttributes` | Get/set file attributes |
| `VF_Dir` | Directory operations (CHECK_DIR, CREATE_DIR, etc.) |
| `VF_Open` | Open file |
| `VF_Read` | Read file data |
| `VF_Write` | Write file data |
| `VF_Close` | Close file handle |
| `VF_Rename` | Rename file |
| `VF_Delete` | Delete file |
| `VF_Seek` | Seek in file |
| `SunConnectNetResource` | Connect network resource |

**Network Resource Types:**
| Flag | Meaning |
|------|---------|
| `RESOPT_UNC_REQUEST` | UNC path query |
| `RESOPT_DEV_ATTACH` | Device attach |
| `RESOPT_UNC_CONNECT` | UNC connection |

### 3. Windows NT/2000 FSD (sunfsd.sys)

| Property | Value |
|----------|-------|
| Type | Windows NT Kernel Mode Driver |
| Location | `drivers/winnt/oem/$$/system32/drivers/sunfsd.sys` |
| Build Date | Sep 29 2000 |
| Source Path | `H:\src\penguin\sw\intel\winnt\fsdnt\` |
| Host Library | `libfsdnt.so.1.1` |

**Source Files (from debug strings):**
- `sunfsdin.c` - Driver initialization
- `create.c` - File creation/open handling

**Key Functions:**
- `DriverEntry` - Driver initialization
- File create/open handling
- Directory enumeration
- Share access management

### 4. Host-Side Libraries

#### libredir.so.1.0 (DOS Redirector Host)

| Export | Purpose |
|--------|---------|
| `NewRedirDispatcher` | Create redirector protocol dispatcher |
| `redir_entry` | Main entry point for operations |

**Operations (from debug strings):**

| Operation | Description |
|-----------|-------------|
| `Lock` | Lock file region (byte-range locking) |
| `UnLock` | Unlock file region |
| `CreateFile` | Create new file |
| `DeleteFile` | Delete file |
| `CreateDir` | Create directory |
| `RmDir` | Remove directory |
| `ChDir` | Change directory |
| `StatVfs` | Get filesystem statistics |
| `StatFile` | Get file statistics |
| `Open` | Open existing file |
| `ExtendOpen` | Extended open (with sharing modes) |
| `SetAttr` | Set file attributes |
| `ValidatePath` | Validate pathname |
| `Access` | Check access permissions |
| `SSrch1st` | Search first (find first file) |
| `DSrchnxt` | Search next (find next file) |
| `FTruncate` | Truncate file |
| `Write` | Write data |
| `Read` | Read data |
| `Lseek` | Seek in file |
| `Gdatetime` | Get date/time |
| `DDostohosttime` | Convert DOS time to host time |
| `Stimeclose` | Set time on close |
| `RenameFile` | Rename file |
| `env()` | Environment variable expansion |

**Environment Variables:**
| Variable | Purpose |
|----------|---------|
| `REDIRVERBOSE` | Enable debug output |
| `DEVEXTENSIONS` | Device extension handling |
| `HOME` | User home directory |

**Valid Filename Characters:**
```
!#$%&@^_~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ
```

#### libfsd95.so.1.1 (Windows 95 FSD Host)

| Export | Purpose |
|--------|---------|
| `NewFsdDispatcher` | Create Win95 FSD protocol dispatcher |
| `fsd_entry` | Main entry point |

**Operations:**

| Request | Description |
|---------|-------------|
| `Rename` | Rename file/directory |
| `Delete` | Delete file |
| `Stat File/Dir` | Get file/directory info |
| `Remove Dir` | Remove directory |
| `Create Dir` | Create directory |
| `Open Dir` | Open directory for enumeration |
| `ReadDir` | Read directory entries |
| `Close Dir` | Close directory handle |
| `Open` | Open file |
| `Read` | Read file data |
| `Write` | Write file data |
| `Close` | Close file handle |
| `LSeek` | Seek in file |
| `Create File` | Create new file |
| `SetAttr` | Set file attributes |
| `StatVfs` | Get VFS statistics |
| `FTruncate` | Truncate file |
| `UDTime` | Update date/time |
| `Lock` | Byte-range lock |
| `DebugOnOff` | Toggle debug mode |
| `ReadLink` | Read symbolic link |

**Path Cache:**
- Uses `PCacheAdd`, `PCacheFind`, `PCacheFree` for caching
- Error: "SunPC: PCacheAdd: Pathcache is full!"

**Environment Variables:**
| Variable | Purpose |
|----------|---------|
| `FSDVERBOSE` | Enable verbose debug output |
| `DEVEXTENSIONS` | Device extension mode |

#### libfsdnt.so.1.1 (Windows NT FSD Host)

| Export | Purpose |
|--------|---------|
| `NewFsdNTDispatcher` | Create WinNT FSD protocol dispatcher |

**NT-Specific Operations:**

| Request | Description |
|---------|-------------|
| `UpdateShareAccess` | Update file sharing access |
| `RemoveShareAccess` | Remove share access entry |
| `Rename` | Rename file |
| `Open NT Dir` | Open directory (NT semantics) |
| `Read NT Dir` | Read directory with caching |
| `GetNTPartialDir` | Partial directory read |
| `ReadNTDirEx` | Extended directory read |

**NT Features:**
- Directory caching: `ReadDirCache`
- File sharing: `SharingEnabled`/`SharingDisabled`
- OS revision tracking: `os_revlevel` (5.5.1)

**Environment Variables:**
| Variable | Purpose |
|----------|---------|
| `FSDNTVERBOSE` | Enable verbose debug output |
| `ENABLE_SUNPCI_LOCKING` | Enable file locking support |
| `DEVEXTENSIONS` | Device extension mode |

## Protocol Details

### Dispatcher Architecture

All FSD libraries use a common plugin architecture:

```c
// Initialization
void *dispatcher = NewXxxDispatcher();
InitDispatcher(core_dispatcher, dispatcher);

// Function table
typedef struct {
    // Function pointers copied via CopyFuncs()
} InheritedFuncs;
```

### DOS ↔ Host Time Conversion

DOS time format:
- Date: Bits 0-4: Day, 5-8: Month, 9-15: Year (since 1980)
- Time: Bits 0-4: Seconds/2, 5-10: Minutes, 11-15: Hours

Functions:
- `host_to_dostime` - Unix time → DOS time
- `host_dos_to_host_time` - DOS time → Unix time

### File Attribute Mapping

| DOS Attribute | Value | Unix Mapping |
|---------------|-------|--------------|
| Read-only | 0x01 | Remove write bits |
| Hidden | 0x02 | Prefix with `.` |
| System | 0x04 | (No direct equivalent) |
| Directory | 0x10 | `S_ISDIR()` |
| Archive | 0x20 | (Simulated) |

Function: `Attribute2Mode()` - Convert between DOS attributes and Unix mode bits.

### Path Translation

| Guest Path | Host Path |
|------------|-----------|
| `F:\` | `$SUNPCIIHOME` (/opt/SUNWspci) |
| `R:\` | `/` (root) |
| `H:\` | `~` ($HOME) |
| `X:\path\file.txt` | Mapped Unix path with translation |

**Path Validation:**
- `host_validate_pathname` - Check path validity
- `unix_validate_path` - Unix-specific validation
- `NValidatePath` - Normalized validation

**Path Manipulation:**
- `host_dos_to_host_name` - DOS 8.3 to Unix name
- `host_concat` - Path concatenation
- `resolve_any_net_join` - Network path resolution

### File Locking

Byte-range locking supported:
- `host_lock` - Acquire lock
- `host_unlock` - Release lock
- `mandatory_lock` - Mandatory lock mode
- `check_access_sharing` - Sharing mode check

Lock parameters:
- File descriptor (FD)
- Start offset
- Length
- Command (lock type)

## Implementation Considerations for Rising Sun

### Priority Components

1. **Path Translation Layer**
   - Map Windows drive letters to Linux paths
   - Handle 8.3 filename conversion
   - Support long filenames (LFN)

2. **File Operation Handlers**
   - Open/Close with sharing modes
   - Read/Write with proper buffering
   - Directory enumeration
   - Attribute get/set

3. **Time Conversion**
   - DOS ↔ Unix time conversion
   - Handle timezone differences

4. **Locking Support**
   - Byte-range locking via `fcntl()`
   - Sharing mode emulation

### Proposed Rust Implementation

```rust
/// File system redirector dispatcher
pub struct FsRedirector {
    /// Mounted drive mappings
    drive_map: HashMap<char, PathBuf>,
    /// Open file handles
    handles: HashMap<u32, OpenFile>,
    /// Path cache
    cache: PathCache,
}

/// Open file tracking
struct OpenFile {
    fd: File,
    path: PathBuf,
    share_mode: ShareMode,
    access_mode: AccessMode,
}

/// File operations interface
trait FsOperations {
    fn open(&mut self, path: &str, mode: OpenMode) -> Result<u32>;
    fn read(&mut self, handle: u32, buf: &mut [u8]) -> Result<usize>;
    fn write(&mut self, handle: u32, buf: &[u8]) -> Result<usize>;
    fn close(&mut self, handle: u32) -> Result<()>;
    fn seek(&mut self, handle: u32, pos: i64, whence: Whence) -> Result<u64>;
    fn stat(&self, path: &str) -> Result<FileInfo>;
    fn readdir(&mut self, path: &str) -> Result<Vec<DirEntry>>;
    // ... etc
}
```

### Guest Driver Strategy

| Guest OS | Strategy |
|----------|----------|
| DOS | Port `redir.sys` concepts, implement as driver or TSR |
| Windows 9x | Create VxD or use existing protocol |
| Windows NT/2000 | Create kernel FS driver or port sunfsd.sys |
| Linux Guest | Not needed (use shared folders via other means) |

## File Listing

### Host Libraries
| File | Size | Purpose |
|------|------|---------|
| `lib/libredir.so.1.0` | 57 KB | DOS redirector host support |
| `lib/libfsd95.so.1.1` | 44 KB | Windows 95 FSD host support |
| `lib/libfsdnt.so.1.1` | 25 KB | Windows NT FSD host support |

### Guest Drivers (DOS)
| File | Size | Purpose |
|------|------|---------|
| `defaults/7.01/sunpc/redir.sys` | ~10 KB | DOS network redirector |
| `defaults/7.01/sunpc/sunpcnet.exe` | ~5 KB | Drive mapping utility |

### Guest Drivers (Windows 95/98)
| File | Purpose |
|------|---------|
| `drivers/win95/sunfsd.vxd` | IFS driver |
| `drivers/win95/sunfsd.inf` | Installation INF |

### Guest Drivers (Windows NT/2000)
| File | Purpose |
|------|---------|
| `drivers/winnt/oem/$$/system32/drivers/sunfsd.sys` | NT4 FSD driver |
| `drivers/winnt/oem/W2K/patch/sunfsd.sys` | Windows 2000 FSD driver |

## Open Questions

1. What is the exact binary protocol between guest drivers and host libraries?
2. How is the RMIPC (Remote IPC) mechanism implemented?
3. What is the maximum number of simultaneous open files supported?
4. How are symbolic links handled (if at all)?
5. What is the performance impact of the path cache?
6. How are file change notifications propagated?

## References

- Microsoft DOS Programmer's Reference (Network Redirector)
- Windows VxD Programming (IFS Manager)
- Windows NT File System Internals
- POSIX fcntl() locking semantics
