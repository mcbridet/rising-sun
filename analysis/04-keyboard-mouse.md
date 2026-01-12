# Keyboard and Mouse Input Analysis

## Overview

The SunPCI input system translates between Sun/X11 keyboard and mouse events on the host and IBM PC AT-compatible keyboard/mouse protocols on the guest. This involves:

1. **X11 KeySym to DOS Scancode translation** - Keyboard mapping tables
2. **Code Page support** - Character set translations for international keyboards
3. **Mouse protocol translation** - Sun/X11 mouse events to PS/2 or serial mouse packets
4. **Modifier state tracking** - Shift, Ctrl, Alt, Caps Lock synchronization

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Host (X11/Solaris)                              │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                        X11 Server                                 │   │
│  │   KeyPress/KeyRelease    MotionNotify    ButtonPress/Release     │   │
│  └────────────┬─────────────────────┬───────────────────────────────┘   │
│               │                     │                                    │
│               ▼                     ▼                                    │
│  ┌──────────────────────┐  ┌──────────────────────┐                     │
│  │    Keyboard Module   │  │     Mouse Module     │                     │
│  │    (sunpcbinary)     │  │    (sunpcbinary)     │                     │
│  │                      │  │                      │                     │
│  │  ┌────────────────┐  │  │  ┌────────────────┐  │                     │
│  │  │ KeySym→Scancode│  │  │  │ X11→PS/2 Mouse│  │                     │
│  │  │   Translation  │  │  │  │    Protocol   │  │                     │
│  │  └────────────────┘  │  │  └────────────────┘  │                     │
│  │  ┌────────────────┐  │  │                      │                     │
│  │  │  Code Page     │  │  │                      │                     │
│  │  │  Mapping       │  │  │                      │                     │
│  │  └────────────────┘  │  │                      │                     │
│  └──────────┬───────────┘  └──────────┬───────────┘                     │
│             │                         │                                  │
│             ▼                         ▼                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                      Driver Interface                            │    │
│  │         SPCIO_SENDKBD              SPCIO_SENDMOUSE               │    │
│  │                                    SPCIO_SENDABSMOUSE            │    │
│  └────────────────────────────────────┬────────────────────────────┘    │
└───────────────────────────────────────┼──────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           Guest (x86)                                    │
│                                                                          │
│  ┌──────────────────────┐  ┌──────────────────────┐                     │
│  │   Keyboard Buffer    │  │   Mouse Driver       │                     │
│  │   INT 16h / INT 09h  │  │   INT 33h / PS/2     │                     │
│  └──────────────────────┘  └──────────────────────┘                     │
│                                                                          │
│  Windows Guest:                                                          │
│  ┌──────────────────────┐  ┌──────────────────────┐                     │
│  │   sunpci.vxd         │  │   spcmouse.vxd       │                     │
│  │   (keyboard hook)    │  │   (mouse driver)     │                     │
│  └──────────────────────┘  └──────────────────────┘                     │
└─────────────────────────────────────────────────────────────────────────┘
```

## Keyboard Translation

### Key Functions (from sunpcbinary symbols)

| Function | Offset | Purpose |
|----------|--------|---------|
| `NewKeyboard` | 0x24358 | Create keyboard handler |
| `KeyboardDestroy` | 0x24344 | Cleanup keyboard |
| `KeyboardTranslateKey` | 0x22d0c | Main translation function |
| `KeyCodetoKeySym` | 0x20ae4 | X11 keycode to keysym |
| `FindSunPCKeySym` | 0x2167c | Find keysym in table |
| `InitKeymapsFromServer` | 0x2157c | Load keymaps from X server |
| `LoadDosKeymap` | 0x22484 | Load DOS keyboard table |
| `LoadKeysymTable` | 0x21b10 | Load keysym translation |
| `LoadKeysymCPTable` | 0x216e8 | Load code page table |
| `LoadKeysymNewCPTable` | 0x22468 | Load new code page |
| `SendDosKey` | 0x225a4 | Send key to guest |
| `RemapKPKeys` | 0x20c60 | Remap keypad keys |
| `HwInterfaceSendKbdEvent` | 0x2d210 | Send to driver |

### Keytable File Format

Location: `tables/keytables/`

#### Keyboard Layout Files (dos_kb_*)

Format: X11 KeySym → DOS scancode mapping

```
# Format: XK_<name>    <scancode>    [modifier]    [# comment]
#
# Modifiers:
#   any         - Accept any modifier state
#   shift       - Shifted position on keyboard
#   caps        - Caps Lock affected
#   shfl        - Shift Lock affected  
#   alt         - Alt Graphic (AltGr) position

XK_BackSpace            15      any     # DOS MT Backspace
XK_Tab                  16      any
XK_Return               43      any     # DOS MT enter
XK_Escape               110     any
XK_space                61      any     # MT space bar
XK_A                    31      shift caps
XK_a                    31      caps
```

#### Scancode Values

The scancodes follow IBM Enhanced 101/102 keyboard layout:

| Region | Scancode Range | Description |
|--------|----------------|-------------|
| Main Array | 1-58 | Letter keys, numbers, punctuation |
| Function Keys | 112-123 | F1-F12 |
| Navigation | 75-86 | Insert, Delete, Home, End, etc. |
| Numeric Pad | 90-108 | Numpad keys |
| Modifiers | 30, 44, 57, 58, 60, 62, 64 | Shift, Ctrl, Alt, Caps |

#### Supported Keyboard Layouts

| File | Layout | Description |
|------|--------|-------------|
| `dos_kb_us` | US | United States QWERTY |
| `dos_kb_uk` | UK | United Kingdom |
| `dos_kb_de` | German | Germany (QWERTZ) - via `dos_kb_gr` |
| `dos_kb_fr` | French | France (AZERTY) |
| `dos_kb_sp` | Spanish | Spain |
| `dos_kb_it` | Italian | Italy |
| `dos_kb_po` | Portuguese | Portugal |
| `dos_kb_nl` | Dutch | Netherlands |
| `dos_kb_be` | Belgian | Belgium |
| `dos_kb_dk` | Danish | Denmark |
| `dos_kb_no` | Norwegian | Norway |
| `dos_kb_sv` | Swedish | Sweden |
| `dos_kb_su` | Finnish | Finland (Suomi) |
| `dos_kb_sf` | Swiss French | Switzerland (French) |
| `dos_kb_sg` | Swiss German | Switzerland (German) |
| `dos_kb_cf` | Canadian French | Canada (French) |
| `dos_kb_la` | Latin American | Latin America |
| `dos_kb_gf` | German (QWERTY) | Alternative German |

### Code Page Files (dos_cp_*)

Map X11 keysyms to DOS character codes for Compose key sequences:

```
# Format: XK_<name>    <CP_code>    [# comment]

XK_space                32
XK_exclam               33
XK_quotedbl             34
XK_Ccedilla             128     # Ç
XK_udiaeresis           129     # ü
XK_eacute               130     # é
```

#### Supported Code Pages

| File | Code Page | Description |
|------|-----------|-------------|
| `dos_cp_437` | CP437 | US English (default) |
| `dos_cp_850` | CP850 | Multilingual Latin-1 |
| `dos_cp_860` | CP860 | Portuguese |
| `dos_cp_863` | CP863 | Canadian French |
| `dos_cp_865` | CP865 | Nordic |

### Key Translation Mapping Counts

| Keyboard | Mappings |
|----------|----------|
| US | 195 |
| UK | 424 |
| French | 430 |
| German | 433 |
| Belgian | 437 |
| Swedish | 442 |

### Modifier State Tracking

The keyboard module tracks modifier states:

```c
// Modifier states (from debug strings)
#define DOS_SHIFT   0x01
#define DOS_CTRL    0x02
#define DOS_ALT     0x04
#define DOS_CAPS    0x08
#define DOS_NUM     0x10
#define DOS_SCROLL  0x20

// State management
"M state before key s = %d"
"M state after  key s = %d"
"modState = 0x%x"
"required state = NONE"
"required state = ANY"
"required state = DOS_SHIFT"
"required state = DOS_ALT"
"required state = DOS_ALT + DOS_SHIFT"
"ALT+NUMPAD"  // Alt+numpad character entry
```

### Sun Keyboard Support

Detects Sun Type 4/5 keyboards via `TYPE4KBD` environment variable:

| Sun Key | Mapping |
|---------|---------|
| `XK_SunF36` | F11 (scancode 122) |
| `XK_SunF37` | F12 (scancode 123) |
| Sun Compose | Enables compose sequences |

## Mouse Translation

### Key Functions (from sunpcbinary symbols)

| Function | Offset | Purpose |
|----------|--------|---------|
| `NewMouse` | 0x2f4f4 | Create mouse handler |
| `MouseDestroy` | 0x2f4e0 | Cleanup mouse |
| `MouseAttachMouse` | 0x2ec2c | Attach to X11 pointer |
| `MouseSendMousePacket` | 0x2ea08 | Send PS/2 packet |
| `MouseSendAbsMouseEvent` | 0x2ede8 | Send absolute position |
| `MouseSendOldMouseEvent` | 0x2f124 | Legacy event format |
| `MouseSetMouseMode` | 0x2e8f8 | Set mouse mode |
| `HwInterfaceSendMouseData` | 0x2d5e0 | Send to driver |
| `HwInterfaceSendAbsMousePacket` | 0x2d578 | Send absolute to driver |

### Driver IOCTLs

| IOCTL | Purpose |
|-------|---------|
| `SPCIO_SENDKBD` | Send keyboard scancode |
| `SPCIO_SENDMOUSE` | Send relative mouse packet |
| `SPCIO_SENDABSMOUSE` | Send absolute mouse position |

### Mouse Modes

From debug strings:

```
"Attach Mouse button pushed!"
" (Mouse Attached)"
"SPCIO_SENDABSMOUSE failed"
"SPCIO_SENDMOUSE failed"
"USESPCMOUSE"
```

### Mouse Packet Format

PS/2 Mouse Packet (3 bytes):
```
Byte 0: [YO XO YS XS 1 MB RB LB]
        YO/XO = overflow
        YS/XS = sign bits
        MB/RB/LB = buttons
Byte 1: X movement (8-bit signed)
Byte 2: Y movement (8-bit signed)
```

### Guest Mouse Drivers

| Driver | Platform | Purpose |
|--------|----------|---------|
| `spcmouse.vxd` | Windows 95/98 | PS/2 mouse emulation |
| `sermouse.sys` | Windows NT | Serial mouse driver |

The Windows NT driver notes:
```
"SerMouse.NT4.Sys built on Sep 29 2000"
"Got that 5th byte"  // IntelliMouse support?
```

## Driver Interface Details

### Keyboard Event Structure

```c
// Inferred from driver strings and behavior
struct KbdEvent {
    uint8_t scancode;      // DOS scancode
    uint8_t flags;         // Key up/down, extended key
};

// Flags
#define KBD_KEYUP    0x80  // Key release
#define KBD_EXTENDED 0x01  // Extended scancode (E0 prefix)
```

### Mouse Event Structure

```c
// Relative mouse event
struct MouseEvent {
    int8_t  dx;           // X movement
    int8_t  dy;           // Y movement
    uint8_t buttons;      // Button state
};

// Absolute mouse event  
struct AbsMouseEvent {
    uint16_t x;           // Absolute X position
    uint16_t y;           // Absolute Y position
    uint8_t  buttons;     // Button state
};
```

## Rising Sun Implementation Strategy

### 1. Keyboard Translation Module

```rust
// common/src/input/keyboard.rs

use std::collections::HashMap;

/// DOS scancode with modifier requirements
#[derive(Debug, Clone)]
pub struct DosKey {
    pub scancode: u8,
    pub modifier: KeyModifier,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum KeyModifier {
    Any,           // Accept any modifier state
    None,          // No modifiers required
    Shift,         // Shift key required
    Caps,          // Caps Lock affected
    ShiftLock,     // Shift Lock affected
    Alt,           // Alt Graphic (AltGr) required
    ShiftAlt,      // Shift + Alt
}

#[derive(Debug, Clone, Copy, Default)]
pub struct ModifierState {
    pub shift: bool,
    pub ctrl: bool,
    pub alt: bool,
    pub caps_lock: bool,
    pub num_lock: bool,
    pub scroll_lock: bool,
}

pub struct KeyboardTranslator {
    /// X11 keysym → DOS scancode mapping
    keymap: HashMap<u32, DosKey>,
    /// Code page character mapping
    codepage: HashMap<u32, u8>,
    /// Current modifier state
    modifiers: ModifierState,
    /// Layout name
    layout: String,
}

impl KeyboardTranslator {
    pub fn new(layout: &str, codepage: u16) -> Result<Self, Error> {
        let keymap = Self::load_keymap(layout)?;
        let cp = Self::load_codepage(codepage)?;
        Ok(Self {
            keymap,
            codepage: cp,
            modifiers: ModifierState::default(),
            layout: layout.to_string(),
        })
    }
    
    pub fn translate(&mut self, keysym: u32, pressed: bool) -> Option<KeyEvent> {
        // Update modifier state for modifier keys
        self.update_modifiers(keysym, pressed);
        
        // Look up scancode
        if let Some(dos_key) = self.keymap.get(&keysym) {
            if self.modifiers_match(dos_key.modifier) {
                return Some(KeyEvent {
                    scancode: dos_key.scancode,
                    pressed,
                    extended: self.is_extended(dos_key.scancode),
                });
            }
        }
        
        // Try code page for compose characters
        if pressed {
            if let Some(&ch) = self.codepage.get(&keysym) {
                return Some(KeyEvent::character(ch));
            }
        }
        
        None
    }
}

pub struct KeyEvent {
    pub scancode: u8,
    pub pressed: bool,
    pub extended: bool,
}
```

### 2. Keymap Parser

```rust
// common/src/input/keymap.rs

use std::io::BufRead;

pub fn parse_keymap(reader: impl BufRead) -> Result<HashMap<u32, DosKey>, Error> {
    let mut map = HashMap::new();
    
    for line in reader.lines() {
        let line = line?;
        let line = line.trim();
        
        // Skip comments and empty lines
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        
        // Parse: XK_name  scancode  [modifier]  [# comment]
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 {
            continue;
        }
        
        let keysym_name = parts[0];
        let scancode: u8 = parts[1].parse()?;
        
        let modifier = if parts.len() > 2 {
            parse_modifier(parts[2])
        } else {
            KeyModifier::None
        };
        
        let keysym = keysym_from_name(keysym_name)?;
        map.insert(keysym, DosKey { scancode, modifier });
    }
    
    Ok(map)
}

fn parse_modifier(s: &str) -> KeyModifier {
    match s {
        "any" => KeyModifier::Any,
        "shift" => KeyModifier::Shift,
        "caps" => KeyModifier::Caps,
        "shfl" => KeyModifier::ShiftLock,
        "alt" => KeyModifier::Alt,
        _ => KeyModifier::None,
    }
}
```

### 3. Mouse Handler

```rust
// common/src/input/mouse.rs

pub struct MouseState {
    pub x: i32,
    pub y: i32,
    pub buttons: MouseButtons,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct MouseButtons {
    pub left: bool,
    pub right: bool,
    pub middle: bool,
}

pub struct MouseHandler {
    state: MouseState,
    /// Use absolute positioning (for Windows integration)
    absolute_mode: bool,
    /// Screen dimensions for scaling
    screen_width: u32,
    screen_height: u32,
}

impl MouseHandler {
    pub fn handle_motion(&mut self, x: i32, y: i32) -> MousePacket {
        if self.absolute_mode {
            let scaled_x = (x * 0xFFFF) / self.screen_width as i32;
            let scaled_y = (y * 0xFFFF) / self.screen_height as i32;
            self.state.x = x;
            self.state.y = y;
            MousePacket::Absolute {
                x: scaled_x as u16,
                y: scaled_y as u16,
                buttons: self.state.buttons,
            }
        } else {
            let dx = x - self.state.x;
            let dy = y - self.state.y;
            self.state.x = x;
            self.state.y = y;
            MousePacket::Relative {
                dx: dx.clamp(-127, 127) as i8,
                dy: (-dy).clamp(-127, 127) as i8, // Y inverted for PS/2
                buttons: self.state.buttons,
            }
        }
    }
    
    pub fn handle_button(&mut self, button: u8, pressed: bool) -> MousePacket {
        match button {
            1 => self.state.buttons.left = pressed,
            2 => self.state.buttons.middle = pressed,
            3 => self.state.buttons.right = pressed,
            _ => {}
        }
        
        // Return current position with updated buttons
        self.handle_motion(self.state.x, self.state.y)
    }
}

pub enum MousePacket {
    Relative { dx: i8, dy: i8, buttons: MouseButtons },
    Absolute { x: u16, y: u16, buttons: MouseButtons },
}

impl MousePacket {
    /// Encode as PS/2 mouse packet (3 bytes)
    pub fn to_ps2(&self) -> [u8; 3] {
        match self {
            MousePacket::Relative { dx, dy, buttons } => {
                let mut byte0 = 0x08u8; // Always set bit 3
                if buttons.left { byte0 |= 0x01; }
                if buttons.right { byte0 |= 0x02; }
                if buttons.middle { byte0 |= 0x04; }
                if *dx < 0 { byte0 |= 0x10; }
                if *dy < 0 { byte0 |= 0x20; }
                
                [byte0, *dx as u8, *dy as u8]
            }
            MousePacket::Absolute { .. } => {
                // Absolute mode uses different protocol
                [0, 0, 0]
            }
        }
    }
}
```

### 4. Linux Driver IOCTL Interface

```c
// driver/sunpci_input.h

#define SUNPCI_SEND_KEYBOARD    _IOW(SUNPCI_IOCTL_MAGIC, 10, struct sunpci_kbd_event)
#define SUNPCI_SEND_MOUSE       _IOW(SUNPCI_IOCTL_MAGIC, 11, struct sunpci_mouse_event)
#define SUNPCI_SEND_MOUSE_ABS   _IOW(SUNPCI_IOCTL_MAGIC, 12, struct sunpci_mouse_abs)

struct sunpci_kbd_event {
    uint8_t scancode;
    uint8_t flags;         // KBD_KEYUP, KBD_EXTENDED
};

struct sunpci_mouse_event {
    int8_t  dx;
    int8_t  dy;
    uint8_t buttons;
};

struct sunpci_mouse_abs {
    uint16_t x;
    uint16_t y;
    uint8_t  buttons;
};
```

### 5. Qt Input Integration

```rust
// frontend/src/input/mod.rs

use qt_core::{QKeyEvent, QMouseEvent};

pub struct InputHandler {
    keyboard: KeyboardTranslator,
    mouse: MouseHandler,
    daemon_client: DaemonClient,
}

impl InputHandler {
    pub fn handle_key_event(&mut self, event: &QKeyEvent) {
        let keysym = qt_key_to_x11_keysym(event.key());
        let pressed = event.type_() == QEvent::KeyPress;
        
        if let Some(key_event) = self.keyboard.translate(keysym, pressed) {
            self.daemon_client.send_keyboard(key_event);
        }
    }
    
    pub fn handle_mouse_event(&mut self, event: &QMouseEvent) {
        let packet = match event.type_() {
            QEvent::MouseMove => {
                self.mouse.handle_motion(event.x(), event.y())
            }
            QEvent::MouseButtonPress | QEvent::MouseButtonRelease => {
                let pressed = event.type_() == QEvent::MouseButtonPress;
                self.mouse.handle_button(event.button() as u8, pressed)
            }
            _ => return,
        };
        
        self.daemon_client.send_mouse(packet);
    }
}
```

## Scancode Reference

### Main Keyboard Area (Scancodes 1-58)

| Scancode | US Key | Scancode | US Key |
|----------|--------|----------|--------|
| 1 | ` ~ | 31 | A |
| 2 | 1 ! | 32 | S |
| 3 | 2 @ | 33 | D |
| 4 | 3 # | 34 | F |
| 5 | 4 $ | 35 | G |
| 6 | 5 % | 36 | H |
| 7 | 6 ^ | 37 | J |
| 8 | 7 & | 38 | K |
| 9 | 8 * | 39 | L |
| 10 | 9 ( | 40 | ; : |
| 11 | 0 ) | 41 | ' " |
| 12 | - _ | 43 | Enter |
| 13 | = + | 44 | L.Shift |
| 15 | Backspace | 46 | Z |
| 16 | Tab | 47 | X |
| 17 | Q | 48 | C |
| 18 | W | 49 | V |
| 19 | E | 50 | B |
| 20 | R | 51 | N |
| 21 | T | 52 | M |
| 22 | Y | 53 | , < |
| 23 | U | 54 | . > |
| 24 | I | 55 | / ? |
| 25 | O | 57 | R.Shift |
| 26 | P | 58 | L.Ctrl |
| 27 | [ { | 60 | L.Alt |
| 28 | ] } | 61 | Space |
| 29 | \ \| | 62 | R.Alt |
| 30 | Caps Lock | 64 | R.Ctrl |

### Function Keys (112-123)

| Scancode | Key |
|----------|-----|
| 112 | F1 |
| 113 | F2 |
| 114 | F3 |
| 115 | F4 |
| 116 | F5 |
| 117 | F6 |
| 118 | F7 |
| 119 | F8 |
| 120 | F9 |
| 121 | F10 |
| 122 | F11 |
| 123 | F12 |

### Navigation Cluster (75-86)

| Scancode | Key |
|----------|-----|
| 75 | Insert |
| 76 | Delete |
| 79 | Left Arrow |
| 80 | Home |
| 81 | End |
| 83 | Up Arrow |
| 84 | Down Arrow |
| 85 | Page Up |
| 86 | Page Down |
| 89 | Right Arrow |

### Numeric Keypad (90-108)

| Scancode | Key |
|----------|-----|
| 90 | Num Lock |
| 91 | 7 / Home |
| 92 | 4 / Left |
| 93 | 1 / End |
| 95 | / |
| 96 | 8 / Up |
| 97 | 5 |
| 98 | 2 / Down |
| 99 | 0 / Ins |
| 100 | * |
| 101 | 9 / PgUp |
| 102 | 6 / Right |
| 103 | 3 / PgDn |
| 104 | . / Del |
| 105 | - |
| 106 | + |
| 108 | Enter |

### Special Keys

| Scancode | Key |
|----------|-----|
| 110 | Escape |
| 124 | Print Screen |
| 125 | Scroll Lock |
| 126 | Pause/Break |

## Files to Create

| Path | Purpose |
|------|---------|
| `common/src/input/mod.rs` | Input module root |
| `common/src/input/keyboard.rs` | Keyboard translation |
| `common/src/input/keymap.rs` | Keymap file parser |
| `common/src/input/codepage.rs` | Code page support |
| `common/src/input/mouse.rs` | Mouse handling |
| `common/src/input/scancodes.rs` | Scancode constants |
| `daemon/src/handlers/input.rs` | Input event handler |
| `frontend/src/input/mod.rs` | Qt input integration |
| `data/keymaps/` | Keyboard layout files |
| `data/codepages/` | Code page files |

## References

- [PS/2 Keyboard Interface](https://wiki.osdev.org/PS/2_Keyboard)
- [PS/2 Mouse Interface](https://wiki.osdev.org/PS/2_Mouse)
- [IBM Enhanced Keyboard](https://www.win.tue.nl/~aeb/linux/kbd/scancodes.html)
- [X11 Keysyms](https://www.cl.cam.ac.uk/~mgk25/ucs/keysyms.txt)
- [DOS Code Pages](https://en.wikipedia.org/wiki/Code_page_437)
