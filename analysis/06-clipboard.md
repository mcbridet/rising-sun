# Clipboard Integration Analysis

## Overview

The SunPCi clipboard integration provides seamless copy/paste functionality between the Solaris host (X11/Motif) and the Windows/DOS guest. The system uses a bidirectional protocol where clipboard changes on either side trigger updates to the other.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Guest (x86 Windows)                            │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                     Windows Clipboard                               │ │
│  │               (CF_TEXT, CF_UNICODETEXT, etc.)                       │ │
│  └────────────────────────────────┬───────────────────────────────────┘ │
│                                   │                                      │
│                                   ▼                                      │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      sunclip.exe                                    │ │
│  │                                                                     │ │
│  │  ┌──────────────────┐  ┌─────────────────┐  ┌──────────────────┐   │ │
│  │  │ Clipboard Viewer │  │  ClipThread     │  │ Bridge Driver    │   │ │
│  │  │ Chain Hook       │  │  (Event Loop)   │  │ Communication    │   │ │
│  │  │ (WM_DRAWCLIPBOARD)│  │                 │  │                  │   │ │
│  │  └────────┬─────────┘  └────────┬────────┘  └────────┬─────────┘   │ │
│  │           │                     │                    │              │ │
│  │           └─────────────────────┼────────────────────┘              │ │
│  │                                 │                                   │ │
│  │                  Events:        │                                   │ │
│  │                  0 = Exit       │                                   │ │
│  │                  1 = OnDrawClipboard (Guest→Host)                   │ │
│  │                  2 = ReadDataFromSparc (Host→Guest)                 │ │
│  │                                 │                                   │ │
│  └─────────────────────────────────┼───────────────────────────────────┘ │
│                                    │                                     │
│                     SUNPCI_REGCOPYPASTE ioctl                           │
│                                    │                                     │
└────────────────────────────────────┼─────────────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Host (SPARC/Solaris)                             │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                       sunpcbinary                                 │   │
│  │                                                                   │   │
│  │  ┌────────────────────────────────────────────────────────────┐  │   │
│  │  │                  libcopypaste.so.1.1                        │  │   │
│  │  │                                                             │  │   │
│  │  │  ┌──────────────────┐  ┌────────────────────────────────┐  │  │   │
│  │  │  │ CopyPasteDispatcher│  │ Operation Handlers            │  │  │   │
│  │  │  │ (Protocol Router)  │  │                                │  │  │   │
│  │  │  │                   │  │  ProcSendCutBuffer (Guest→Host)│  │  │   │
│  │  │  │                   │  │  ProcGetCutBuffer  (Host→Guest)│  │  │   │
│  │  │  └───────────────────┘  └────────────────────────────────┘  │  │   │
│  │  └─────────────────────────────────┬──────────────────────────┘  │   │
│  │                                    │                             │   │
│  │                                    ▼                             │   │
│  │  ┌────────────────────────────────────────────────────────────┐  │   │
│  │  │                    User Interface Layer                     │  │   │
│  │  │                                                             │  │   │
│  │  │  UserInterfaceSetXClipboardData()                           │  │   │
│  │  │  UserInterfaceGetXClipboardData()                           │  │   │
│  │  │  CheckXClipboard()                                          │  │   │
│  │  └─────────────────────────────────┬──────────────────────────┘  │   │
│  │                                    │                             │   │
│  │                                    ▼                             │   │
│  │  ┌────────────────────────────────────────────────────────────┐  │   │
│  │  │                   X11/Motif Clipboard                       │  │   │
│  │  │                                                             │  │   │
│  │  │  XmClipboardStartCopy / XmClipboardCopy / XmClipboardEndCopy│  │   │
│  │  │  XmClipboardStartRetrieve / XmClipboardRetrieve / ...       │  │   │
│  │  │  XmClipboardLock / XmClipboardUnlock                        │  │   │
│  │  │  XmClipboardInquireLength                                   │  │   │
│  │  └────────────────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Analysis

### 1. Host Library (libcopypaste.so.1.1)

| Property | Value |
|----------|-------|
| Size | 8,456 bytes (~8 KB) |
| Location | `lib/libcopypaste.so.1.1` |
| Build Date | Oct 4, 2000 |
| Source | `CopyPasteDispatcher.c` |
| Compiler | WorkShop Compilers 4.2 (Oct 1996) |

#### Exported Functions

| Symbol | Type | Purpose |
|--------|------|---------|
| `NewCopyPasteDispatcher` | Export | Create clipboard dispatcher instance |

#### Imported Functions

| Symbol | Purpose |
|--------|---------|
| `InitDispatcher` | Register with core dispatcher |
| `CopyFuncs` | Copy function table from parent |
| `pUserInterface` | UI callback interface pointer |
| `malloc`, `calloc`, `free` | Memory management |
| `memcpy`, `strlen` | String operations |
| `printf`, `fprintf` | Debug output |
| `getenv` | Environment variable access |

#### Dispatch Tables

The library implements three dispatch tables for different modes:

| Table | Purpose |
|-------|---------|
| `NativeDispatchTable` | Normal operation |
| `SwappedDispatchTable` | Byte-swapped operation (endian conversion) |
| `DebugDispatchTable` | Debug mode with logging |

#### Protocol Operations

| Operation | Function | Direction | Description |
|-----------|----------|-----------|-------------|
| Noop | `ProcNoop` | — | No operation / keepalive |
| SendCutBuffer | `ProcSendCutBuffer` | Guest → Host | Send clipboard data from guest |
| GetCutBuffer | `ProcGetCutBuffer` | Host → Guest | Get clipboard data for guest |

**Debug Output Examples:**
```
ProcSendCutBuffer sending %d bytes
SendCutBuffer sending %d bytes
XClipboardData: %s
```

#### Environment Variables

| Variable | Purpose |
|----------|---------|
| `DEVEXTENSIONS` | Enable device extensions mode |

### 2. Main Integration (sunpcbinary)

The `sunpcbinary` uses Motif clipboard API (Xm) for X11 integration:

#### Clipboard Functions

| Function | Purpose |
|----------|---------|
| `SunPCSetXClipboardData` | Set X clipboard from guest data |
| `UserInterfaceSetXClipboardData` | UI layer clipboard write |
| `UserInterfaceGetXClipboardData` | UI layer clipboard read |
| `SetXClipboardData` | Internal clipboard setter |
| `DoSetXClipboardData` | Actual clipboard write |
| `GetXClipboardData` | Get clipboard content |
| `CheckXClipboard` | Poll clipboard for changes |

#### Motif Clipboard API Usage

| Motif Function | Purpose |
|----------------|---------|
| `XmClipboardLock` | Lock clipboard for exclusive access |
| `XmClipboardUnlock` | Release clipboard lock |
| `XmClipboardStartCopy` | Begin clipboard write operation |
| `XmClipboardCopy` | Write data to clipboard |
| `XmClipboardEndCopy` | Complete clipboard write |
| `XmClipboardInquireLength` | Query clipboard data length |
| `XmClipboardStartRetrieve` | Begin clipboard read operation |
| `XmClipboardRetrieve` | Read data from clipboard |
| `XmClipboardEndRetrieve` | Complete clipboard read |

### 3. Guest Utility (sunclip.exe)

#### Windows 95/98 Version

| Property | Value |
|----------|-------|
| Size | 136,260 bytes (~133 KB) |
| Location | `drivers/win95/sunclip.exe` |
| Type | Win32 GUI Application |
| Host Library | `libcopypaste.so.1.1` |

#### Windows NT/2000 Version

| Property | Value |
|----------|-------|
| Location | `drivers/winnt/oem/$$/system32/sunclip.exe` |
| Build Info | "SunClip.exe FREE built on %s %s" |
| Device Path | `\\.\libcopypaste.so.1.1` |

#### Clipboard Viewer Chain

`sunclip.exe` inserts itself into the Windows clipboard viewer chain:

```c
// Register as clipboard viewer
SetClipboardViewer(hWnd);

// Pass messages to next viewer
ChangeClipboardChain(hWnd, hNextClipWnd);
```

#### Threading Model

The utility uses a dedicated clipboard thread (`ClipThread`):

```
ClipThread Event Loop:
┌─────────────────────────────────────────┐
│  Wait for event (WaitForSingleObject)   │
└────────────────────┬────────────────────┘
                     │
         ┌───────────┼───────────┐
         ▼           ▼           ▼
    Event 0      Event 1     Event 2
    ────────     ────────    ────────
    Exit         OnDraw      ReadFrom
    Thread       Clipboard   Sparc
                 (Send)      (Receive)
```

**Event Types:**
| Event | Action | Description |
|-------|--------|-------------|
| 0 | Exit | Terminate clipboard thread |
| 1 | OnDrawClipboard | Guest clipboard changed, send to host |
| 2 | ReadDataFromSparc | Host clipboard changed, update guest |

#### Key Functions

| Function | Purpose |
|----------|---------|
| `WinMain` | Entry point, opens bridge driver |
| `SunclipDialogProc` | Main dialog window procedure |
| `OnDrawClipboard` | Handle WM_DRAWCLIPBOARD |
| `SendToSparc` | Send clipboard data to host |
| `CopyToClipboard` | Copy received data to Windows clipboard |
| `ReadDataFromSparc` | Receive clipboard data from host |
| `CreateSunclipMenu` | Create system tray menu |
| `RegisterWithBridgeDriver` | Register with sunpci.vxd |

#### Bridge Driver Communication

```c
// Registration with bridge driver
RegisterWithBridgeDriver(SUNPCI_REGCOPYPASTE, ...);

// If registration fails:
"RegisterWithBridgeDriver: SUNPCI_REGCOPYPASTE Failed!"
```

#### Windows Clipboard API Usage

| API Function | Purpose |
|--------------|---------|
| `OpenClipboard` | Open clipboard for access |
| `CloseClipboard` | Release clipboard |
| `EmptyClipboard` | Clear clipboard contents |
| `SetClipboardData` | Set clipboard data |
| `GetClipboardData` | Get clipboard data |
| `IsClipboardFormatAvailable` | Check for specific format |
| `CountClipboardFormats` | Count available formats |
| `SetClipboardViewer` | Join clipboard viewer chain |
| `ChangeClipboardChain` | Leave clipboard viewer chain |

### 4. Bridge Driver Integration (sunpci.vxd)

The bridge driver provides the ioctl interface for clipboard registration:

#### Ioctl Commands

| Command | Purpose |
|---------|---------|
| `SUNPCI_REGCOPYPASTE` | Register clipboard handler |

**Error Messages:**
```
SunPCi Ioctl(SUNPCI_REGCOPYPASTE) bad arg
SunPCi Ioctl(SUNPCI_REGCOPYPASTE) bad arg ptr
SUNPCI_REGCOPYPASTE: Error: Softc is NULL
```

## Protocol Details

### Data Flow: Guest → Host (Copy)

```
1. User copies text in Windows application
2. Windows updates clipboard (CF_TEXT)
3. sunclip.exe receives WM_DRAWCLIPBOARD
4. ClipThread wakes on Event 1
5. OnDrawClipboard() called
6. GetClipboardData(CF_TEXT) retrieves text
7. SendToSparc() sends via bridge driver
8. Bridge driver triggers ProcSendCutBuffer in libcopypaste.so
9. UserInterfaceSetXClipboardData() writes to X clipboard
10. XmClipboardCopy() updates Motif clipboard
```

### Data Flow: Host → Guest (Paste)

```
1. User copies text in Solaris/X11 application
2. X11 clipboard updated
3. CheckXClipboard() detects change (polling or selection event)
4. UserInterfaceGetXClipboardData() reads X clipboard
5. ProcGetCutBuffer sends data to guest
6. Bridge driver signals Event 2 to sunclip.exe
7. ClipThread wakes, calls ReadDataFromSparc()
8. CopyToClipboard() writes to Windows clipboard
9. OpenClipboard / EmptyClipboard / SetClipboardData / CloseClipboard
```

### Message Format (Inferred)

The `reqSizes` array in libcopypaste.so suggests fixed-size request headers:

```c
struct CopyPasteRequest {
    uint32_t opcode;      // Operation type
    uint32_t length;      // Data length
    // followed by data bytes
};

// Opcodes (inferred):
// 0 = Noop
// 1 = SendCutBuffer (guest clipboard data)
// 2 = GetCutBuffer (request host clipboard)
```

### Supported Clipboard Formats

| Platform | Format | Notes |
|----------|--------|-------|
| Windows | CF_TEXT | Plain ASCII text |
| Windows | CF_UNICODETEXT | Unicode text (NT/2000) |
| X11/Motif | STRING | Plain text |
| X11/Motif | COMPOUND_TEXT | Locale-encoded text |

**Note:** Only text formats are supported. Rich content (images, RTF) is not transferred.

## Implementation Considerations for Rising Sun

### Linux Host Implementation

Replace Motif clipboard API with modern alternatives:

| Original (Motif) | Replacement (Qt6/X11) |
|------------------|----------------------|
| `XmClipboardCopy` | `QClipboard::setText()` |
| `XmClipboardRetrieve` | `QClipboard::text()` |
| `XmClipboardLock` | Qt handles internally |

Alternatively, use raw X11 selections:
- PRIMARY selection (middle-click paste)
- CLIPBOARD selection (Ctrl+C/V)

### Proposed Rust/Qt Implementation

```rust
/// Clipboard synchronization manager
pub struct ClipboardSync {
    /// Qt clipboard instance
    clipboard: QClipboard,
    /// Last known clipboard content hash
    last_hash: u64,
    /// Channel for guest clipboard updates
    guest_tx: Sender<ClipboardData>,
    guest_rx: Receiver<ClipboardData>,
}

/// Clipboard data container
pub struct ClipboardData {
    /// Text content (UTF-8)
    pub text: String,
    /// Source (host or guest)
    pub source: ClipboardSource,
}

impl ClipboardSync {
    /// Poll for host clipboard changes
    pub fn check_host_clipboard(&mut self) -> Option<ClipboardData> {
        let text = self.clipboard.text();
        let hash = calculate_hash(&text);
        if hash != self.last_hash {
            self.last_hash = hash;
            Some(ClipboardData {
                text,
                source: ClipboardSource::Host,
            })
        } else {
            None
        }
    }
    
    /// Handle clipboard data from guest
    pub fn set_from_guest(&mut self, data: ClipboardData) {
        self.last_hash = calculate_hash(&data.text);
        self.clipboard.setText(&data.text);
    }
}
```

### Guest Integration Strategy

| Guest OS | Strategy |
|----------|----------|
| DOS | Not applicable (no clipboard concept) |
| Windows 3.x | Could implement, but low priority |
| Windows 95/98 | Port sunclip.exe or implement VxD hook |
| Windows NT/2000 | Port sunclip.exe Win32 version |
| Windows XP+ | Modern Win32 clipboard API |

### Protocol Messages (Rising Sun)

```rust
/// Clipboard protocol messages
#[derive(Debug, Serialize, Deserialize)]
pub enum ClipboardMessage {
    /// No operation / keepalive
    Noop,
    /// Guest is sending clipboard data
    SendClipboard { text: String },
    /// Request clipboard data from host
    GetClipboard,
    /// Host is sending clipboard data
    ClipboardData { text: String },
    /// Clipboard is empty
    ClipboardEmpty,
}
```

## File Listing

### Host Components
| File | Size | Purpose |
|------|------|---------|
| `lib/libcopypaste.so.1.1` | 8 KB | Clipboard protocol dispatcher |

### Guest Components (Windows 95/98)
| File | Size | Purpose |
|------|------|---------|
| `drivers/win95/sunclip.exe` | 133 KB | Clipboard sync utility |

### Guest Components (Windows NT/2000)
| File | Purpose |
|------|---------|
| `drivers/winnt/oem/$$/system32/sunclip.exe` | NT4 clipboard utility |
| `drivers/winnt/oem/W2K/patch/sunclip.exe` | Windows 2000 version |
| `drivers/winnt/patch/sunclip.exe` | Patched version |

## Open Questions

1. How does the host detect clipboard changes? (Polling vs. X11 selection events)
2. What is the maximum clipboard data size supported?
3. Are there any character encoding conversions (e.g., Windows-1252 ↔ ISO-8859-1)?
4. Is there support for clipboard history or multiple formats?
5. How are clipboard change notifications delivered to sunclip.exe?
6. What happens when both sides change clipboard simultaneously?

## References

- X11 ICCCM (Inter-Client Communication Conventions Manual) - Selections
- Motif Programmer's Reference - Clipboard Functions
- Windows SDK - Clipboard Functions
- MSDN - Clipboard Viewer Chain
