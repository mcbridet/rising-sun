# Display Emulation Analysis

## Overview

The SunPCI display system provides a complete VGA/SVGA implementation that bridges the x86 guest with the Solaris X11 display system. The architecture consists of:

1. **BIOS Video Services** (INT 10h) - Text mode and basic VGA
2. **VGA Text Manager** - Text mode rendering via X11
3. **Video Dispatcher** - GDI-like graphics operations
4. **Guest Drivers** - Windows display drivers for high-resolution modes

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Guest (x86)                                     │
│  ┌───────────────┐  ┌───────────────┐  ┌──────────────────────────────┐ │
│  │   BIOS ROM    │  │  Windows GDI  │  │    Guest Display Driver      │ │
│  │   INT 10h     │  │               │  │ spcdisp.drv / sunvideo.dll   │ │
│  └───────┬───────┘  └───────┬───────┘  └──────────────┬───────────────┘ │
│          │                  │                         │                  │
│          └──────────────────┼─────────────────────────┘                  │
│                             │                                            │
│                             ▼                                            │
│                    ┌─────────────────┐                                   │
│                    │ Protocol Layer  │                                   │
│                    └────────┬────────┘                                   │
└─────────────────────────────┼────────────────────────────────────────────┘
                              │ Driver/DMA
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Host (SPARC/Solaris)                            │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                      sunpcbinary                                   │  │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────────────┐  │  │
│  │  │  VGA Text     │  │    Video      │  │    Screen Surface     │  │  │
│  │  │  Manager      │  │  Dispatcher   │  │    Manager            │  │  │
│  │  │ (libvga.so)   │  │(libvideo.so)  │  │  (libvideo.so)        │  │  │
│  │  └───────┬───────┘  └───────┬───────┘  └───────────┬───────────┘  │  │
│  │          │                  │                      │               │  │
│  │          └──────────────────┼──────────────────────┘               │  │
│  │                             │                                      │  │
│  │                             ▼                                      │  │
│  │                    ┌─────────────────┐                             │  │
│  │                    │   X11/Xlib      │                             │  │
│  │                    └────────┬────────┘                             │  │
│  └─────────────────────────────┼─────────────────────────────────────┘  │
└─────────────────────────────────┼────────────────────────────────────────┘
                                  ▼
                         ┌─────────────────┐
                         │  X11 Display    │
                         │ (OpenWindows)   │
                         └─────────────────┘
```

## Library Analysis

### 1. libvga.so.1.1 - VGA Text Mode Manager

Handles BIOS-level video operations and text mode rendering.

#### Key Components

| Component | Purpose |
|-----------|---------|
| `VGATextMgr` | Core text mode management |
| `VGADispatcher` | Protocol message routing |
| `SMITick` | System Management Interrupt handling |

#### Exported Functions

| Function | Purpose |
|----------|---------|
| `NewVGADispatcher` | Create VGA protocol dispatcher |
| `NewVGATextMgr` | Create text mode manager |

#### VGA Register Emulation

The library emulates standard VGA registers:

```c
// Miscellaneous registers
Miscellaneous Output Register    // 0x3C2/0x3CC
Feature Control Register         // 0x3CA/0x3DA
Input Status Register 0          // 0x3C2
Input Status Register 1          // 0x3DA

// CRT Controller (CRTC)
CRT Index Register               // 0x3D4
CRT Data Register                // 0x3D5

// Sequencer
Sequence Index Register          // 0x3C4
Sequence Data Register           // 0x3C5

// Graphics Controller
Graphics Index Register          // 0x3CE
Graphics Data Register           // 0x3CF

// Attribute Controller
Attribute Index Register         // 0x3C0
Attribute Data Register          // 0x3C1

// VGA DAC (Palette)
DAC Read Index Register          // 0x3C7
DAC Write Index Register         // 0x3C8
DAC Data Register                // 0x3C9
DAC PEL Mask Register            // 0x3C6

// Segment Select (Extended)
Segment Select Register 0
Segment Select Register 1
VGA Enable Register
```

#### Protocol Operations (Dispatch Table)

| Function | Offset | Protocol Operation |
|----------|--------|-------------------|
| `DNoop` | 0x3d98 | No-operation |
| `DSetPalette` | 0x3de0 | Set VGA palette |
| `DSetDac` | 0x3e7c | Set DAC registers |
| `DDumpVideoRegs` | 0x3f00 | Debug: dump registers |
| `DSetVideoMode` | 0x46e0 | Set video mode |
| `DSetCursor` | 0x47d0 | Set cursor shape/position |
| `DSetVideoData` | 0x4558 | Write video data |
| `DSMITick` | 0x4844 | SMI timer tick |

#### Dispatch Tables

```c
// Three dispatch tables for different modes
NativeDispatchTable    // SPARC native byte order
DebugDispatchTable     // Debug/verbose mode  
InheritedFuncs         // Base class functions
```

#### Text Mode Rendering

| Function | Purpose |
|----------|---------|
| `VGATextMgrSetMode` | Set text mode (80x25, 40x25, etc.) |
| `VGATextMgrSetCursor` | Set cursor shape |
| `VGATextMgrDrawCursor` | Render cursor |
| `VGATextMgrRepaint` | Repaint text screen |
| `VGATextMgrSetCharacters` | Write character data |
| `VGATextMgrRaiseNotify` | Notify of changes |

#### Bitmap Drawing

| Function | Purpose |
|----------|---------|
| `VGATextMgrDraw8BitBitmap` | 256-color bitmap |
| `VGATextMgrDraw8BitBitmapAs24Bit` | 8→24-bit conversion |
| `VGATextMgrDraw4BitBitmap` | 16-color bitmap |
| `VGATextMgrDraw4BitBitmapAs24Bit` | 4→24-bit conversion |
| `VGATextMgrDraw4BitPlanarBitmap` | Planar VGA mode |
| `VGATextMgrDraw4BitPlanarBitmapAs24Bit` | Planar→24-bit |

#### X11 Integration

Uses Xlib for display:
- `XOpenDisplay` / `XCloseDisplay`
- `XCreateWindow` / `XDestroyWindow`
- `XCreateImage` / `XPutImage`
- `XCreateColormap` / `XStoreColors`
- `XLoadQueryFont` / `XDrawImageString`
- Custom font: `pc8x16s` (8x16 pixel PC font)

### 2. libvideo.so.1.2 - High-Resolution Video

Handles GDI-like graphics operations for Windows display drivers.

#### Exported Functions

| Function | Purpose |
|----------|---------|
| `NewVidDispatcher` | Create video dispatcher |
| `NewScreenSurface` | Create screen surface |
| `NewBitmapSurface` | Create offscreen bitmap |
| `InitDrawingSurface` | Initialize drawing surface |
| `DisplayBitmap` | Display bitmap to X11 |
| `DrawingSurfaceDisplayDeep` | Display deep color surface |
| `DrawingSurfaceDisplayMono` | Display monochrome surface |
| `CreateDither` | Create dithering tables |
| `ScanLR8` / `ScanLR24` | Scanline operations |

#### Protocol Operations

Full GDI-like protocol for Windows display drivers:

**Screen Management**
| Function | Purpose |
|----------|---------|
| `ProcEnableScreen` | Enable display output |
| `ProcDisableScreen` | Disable display output |
| `ProcSaveScreenRect` | Save screen region |
| `ProcGetDepthInfo` | Get color depth info |
| `ProcGetGDIInfo` | Get GDI capabilities |
| `ProcFlush` | Flush pending operations |

**Object Management**
| Function | Purpose |
|----------|---------|
| `ProcCreateSurface` | Create drawing surface |
| `ProcCreatePen` | Create GDI pen |
| `ProcCreateBrush` | Create GDI brush |
| `ProcCreateFont` | Create GDI font |
| `ProcDeleteObject` | Delete GDI object |

**Bitmap Operations**
| Function | Purpose |
|----------|---------|
| `ProcBitBlt` | Bit block transfer |
| `ProcGetBitmapBits` | Read bitmap data |
| `ProcSetBitmapBits` | Write bitmap data |
| `ProcSetMonoBitmapBits` | Write mono bitmap |
| `ProcGetMonoBitmapBits` | Read mono bitmap |
| `ProcSetDIBitsToDevice` | DIB to device transfer |
| `ProcSetDeviceBitmapBits` | Device bitmap data |
| `ProcDecompressDeviceBitmapBits` | RLE decompression |

**Drawing Operations**
| Function | Purpose |
|----------|---------|
| `ProcRectangles` | Draw rectangles |
| `ProcPolygon` | Draw polygon |
| `ProcPolyline` | Draw polyline |
| `ProcPolySpans` | Draw polygon spans |
| `ProcFastBorder` | Fast border drawing |
| `ProcSetPixel` | Set single pixel |
| `ProcGetPixel` | Get single pixel |
| `ProcScanLR` | Scanline operations |

**Text Operations**
| Function | Purpose |
|----------|---------|
| `ProcTextOut` | Draw text (Win95) |
| `ProcTextOutNT` | Draw text (WinNT) |
| `ProcDownloadGlyphs` | Download font glyphs |
| `ProcDownloadGlyphsNT` | Download glyphs (NT) |
| `ProcDumpFont` | Debug font info |

**Palette/Color**
| Function | Purpose |
|----------|---------|
| `ProcSetPalette` | Set color palette |
| `ProcSetPaletteTrans` | Set palette translation |
| `ProcSetClipRects` | Set clipping region |
| `ProcSetCursor` | Set mouse cursor |

#### Color Depth Support

**DIB Converters**
| Function | Purpose |
|----------|---------|
| `DIBConvert1` | 1-bit (monochrome) |
| `DIBConvert4` | 4-bit (16 colors) |
| `DIBConvert8` | 8-bit (256 colors) |
| `DIBConvert24` | 24-bit (TrueColor) |

**Internal Converters**
| Function | Purpose |
|----------|---------|
| `Convert1to1` | Mono → Mono |
| `Convert4to8` | 16 → 256 colors |
| `Convert4to24` | 16 → TrueColor |
| `Convert8to8` | 256 → 256 (palette remap) |
| `Convert8to24` | 256 → TrueColor |
| `Convert24to8` | TrueColor → 256 |
| `Convert24to24` | TrueColor copy |

**Dithering Tables**
| Variable | Purpose |
|----------|---------|
| `dither16` | 16-color dithering |
| `dither16m` | 16-color alternate |
| `dither256` | 256-color dithering |
| `defaultWinColors16` | Windows 16-color palette |
| `defaultWinColors256` | Windows 256-color palette |

## Guest Display Drivers

### Windows 95/98 Drivers

| Driver | Size | Purpose |
|--------|------|---------|
| `spcdisp.drv` | 54 KB | Main display driver |
| `spcdisp.vxd` | 16 KB | Display VxD helper |
| `sis597.drv` | 129 KB | SiS 5597/5598 driver |
| `sismini.vxd` | 22 KB | SiS mini-VDD |

**spcdisp.drv Features:**
- 256-color SVGA driver
- BitBlt operations
- CreateDIBitmap / SetDIBitsToDevice
- ValidateMode for resolution changes
- Hi-res mode switching

### Windows NT/2000 Drivers

| Driver | Size | Purpose |
|--------|------|---------|
| `sunvideo.dll` | 96 KB | Display driver DLL |
| `sunvmini.sys` | 3 KB | Video miniport driver |
| `sisv.sys` | 82 KB | SiS video miniport |
| `sisv256.dll` | 129 KB | SiS 256-color driver |
| `sisv.dll` | 123 KB | SiS display driver |

**sunvideo.dll Operations:**
- `DrvBitBlt` - Bit block transfer
- `DrvStretchBlt` - Stretch blit
- `DrvRealizeBrush` - Brush realization
- `DrvCreateDeviceBitmap` - Device bitmap creation
- `DrvStrokePath` - Path stroking
- `DrvSetPalette` - Palette management
- `DrvAssertMode` - Mode switching
- `DrvGetModes` - Query available modes
- VGA mode zero support (`SpcVidSetVGAModeZero`)

## Supported Resolutions

### Default Configuration

From INF file (Windows NT):
```ini
[Display]
XResolution = 800
YResolution = 600
BitsPerPel = 8
VRefresh = 60
```

### SiS 5597/5598 Modes

The SiS video driver supports multiple refresh rates:

| Mode | Refresh Rates |
|------|---------------|
| BI01-04 (640x480) | 60, 72, 75, 85 Hz |
| BI11-14 (800x600) | 56, 60, 72, 75, 85 Hz |
| BI21-24 (1024x768) | 43, 60, 70, 75, 85 Hz |
| BI31-33 (1280x1024) | 43, 60, 75 Hz |

### BIOS Video Modes

Standard VGA BIOS modes (INT 10h AH=00h):

| Mode | Resolution | Colors | Type |
|------|------------|--------|------|
| 0x00-01 | 40×25 | 16 | Text |
| 0x02-03 | 80×25 | 16 | Text |
| 0x04-06 | 320×200 | 4 | CGA Graphics |
| 0x07 | 80×25 | Mono | Text |
| 0x0D | 320×200 | 16 | EGA Graphics |
| 0x0E | 640×200 | 16 | EGA Graphics |
| 0x0F | 640×350 | Mono | EGA Graphics |
| 0x10 | 640×350 | 16 | EGA Graphics |
| 0x11 | 640×480 | Mono | VGA Graphics |
| 0x12 | 640×480 | 16 | VGA Graphics |
| 0x13 | 320×200 | 256 | VGA Graphics |

### VESA/VBE Modes (Extended)

Via SiS 5598 VESA BIOS:

| Mode | Resolution | Colors |
|------|------------|--------|
| 0x100 | 640×400 | 256 |
| 0x101 | 640×480 | 256 |
| 0x103 | 800×600 | 256 |
| 0x105 | 1024×768 | 256 |
| 0x107 | 1280×1024 | 256 |
| 0x110-0x112 | 640×480 | 32K/64K/16M |
| 0x113-0x115 | 800×600 | 32K/64K/16M |
| 0x116-0x118 | 1024×768 | 32K/64K/16M |
| 0x119-0x11B | 1280×1024 | 32K/64K/16M |

## Protocol Message Format

Based on the dispatcher architecture, video protocol messages follow this format:

```c
struct VideoProtocolMessage {
    uint32_t type;        // Dispatcher type ID
    uint32_t opcode;      // Operation code
    uint32_t length;      // Payload length
    uint8_t  payload[];   // Variable data
};

// Video dispatcher opcodes (approximate, from function order)
enum VideoOpcode {
    VID_NOOP = 0,
    VID_ENABLE_SCREEN,
    VID_DISABLE_SCREEN,
    VID_SET_PALETTE,
    VID_SET_PALETTE_TRANS,
    VID_CREATE_PEN,
    VID_CREATE_BRUSH,
    VID_CREATE_SURFACE,
    VID_CREATE_FONT,
    VID_DELETE_OBJECT,
    VID_GET_BITMAP_BITS,
    VID_SET_BITMAP_BITS,
    VID_SET_MONO_BITMAP_BITS,
    VID_GET_MONO_BITMAP_BITS,
    VID_SET_DIBITSTO_DEVICE,
    VID_SET_DEVICE_BITMAP_BITS,
    VID_DECOMPRESS_DEVICE_BITMAP,
    VID_SET_PIXEL,
    VID_GET_PIXEL,
    VID_POLYGON,
    VID_POLYLINE,
    VID_POLYSPANS,
    VID_RECTANGLES,
    VID_SET_CLIP_RECTS,
    VID_BITBLT,
    VID_TEXTOUT,
    VID_TEXTOUT_NT,
    VID_SAVE_SCREEN_RECT,
    VID_FAST_BORDER,
    VID_FLUSH,
    VID_SCAN_LR,
    VID_SET_CURSOR,
    VID_GET_DEPTH_INFO,
    VID_GET_GDI_INFO,
    VID_DOWNLOAD_GLYPHS,
    VID_DOWNLOAD_GLYPHS_NT,
    VID_DUMP_FONT,
    // ... more
};
```

## Rising Sun Implementation Strategy

### 1. VGA Emulation Layer

```rust
// common/src/video/vga.rs
pub struct VgaState {
    // Registers
    pub misc_output: u8,
    pub feature_ctrl: u8,
    pub crtc: [u8; 25],
    pub sequencer: [u8; 5],
    pub graphics: [u8; 9],
    pub attribute: [u8; 21],
    
    // DAC
    pub dac_read_index: u8,
    pub dac_write_index: u8,
    pub dac_pel_mask: u8,
    pub dac_palette: [[u8; 3]; 256],
    
    // State
    pub mode: VideoMode,
    pub cursor_start: u8,
    pub cursor_end: u8,
    pub cursor_x: u16,
    pub cursor_y: u16,
}

pub enum VideoMode {
    Text40x25,
    Text80x25,
    Graphics320x200x4,
    Graphics320x200x256,
    Graphics640x480x16,
    Graphics640x480x256,
    // VESA modes...
}
```

### 2. Qt Display Widget

```rust
// frontend/src/display/vga_widget.rs
pub struct VgaDisplayWidget {
    framebuffer: Vec<u32>,   // RGBA
    width: u32,
    height: u32,
    dirty_rects: Vec<Rect>,
    
    // Text mode
    text_buffer: Vec<u16>,   // Char + attribute
    font_data: Vec<u8>,      // 8x16 font
    
    // Palette
    palette: [u32; 256],
}

impl VgaDisplayWidget {
    fn render_text_mode(&mut self);
    fn render_graphics_mode(&mut self);
    fn update_palette(&mut self, index: u8, r: u8, g: u8, b: u8);
    fn set_cursor(&mut self, x: u16, y: u16);
}
```

### 3. Video Protocol Handler

```rust
// daemon/src/handlers/video.rs
pub struct VideoDispatcher {
    surfaces: HashMap<u32, Surface>,
    brushes: HashMap<u32, Brush>,
    pens: HashMap<u32, Pen>,
    fonts: HashMap<u32, Font>,
    
    screen: ScreenSurface,
    palette: [u32; 256],
}

impl VideoDispatcher {
    fn handle_message(&mut self, msg: &VideoMessage) -> Result<()> {
        match msg.opcode {
            VID_ENABLE_SCREEN => self.enable_screen(),
            VID_BITBLT => self.do_bitblt(&msg.payload),
            VID_SET_PALETTE => self.set_palette(&msg.payload),
            // ...
        }
    }
}
```

### 4. Linux Kernel DRM Integration

For modern systems, consider using Linux DRM/KMS:

```c
// driver/sunpci_drm.c
// Option: Create a DRM driver for direct rendering
// This would bypass X11 for better performance

struct sunpci_drm_device {
    struct drm_device drm;
    void __iomem *vram;
    size_t vram_size;
    
    struct drm_plane primary_plane;
    struct drm_crtc crtc;
    struct drm_encoder encoder;
    struct drm_connector connector;
};
```

## Files to Create

| Path | Purpose |
|------|---------|
| `common/src/video/mod.rs` | Video module root |
| `common/src/video/vga.rs` | VGA state and registers |
| `common/src/video/modes.rs` | Video mode definitions |
| `common/src/video/palette.rs` | Color palette handling |
| `common/src/video/protocol.rs` | Video protocol messages |
| `daemon/src/handlers/vga.rs` | VGA handler implementation |
| `daemon/src/handlers/video.rs` | High-res video handler |
| `frontend/src/display/mod.rs` | Display module root |
| `frontend/src/display/vga_widget.rs` | Qt VGA display widget |
| `frontend/src/display/renderer.rs` | Framebuffer renderer |

## PC Font Data

The package includes `pc8x16s.bdf` - a standard PC 8×16 pixel font.
This should be converted to a binary format for the Qt frontend.

## References

- [VGA Hardware](https://wiki.osdev.org/VGA_Hardware)
- [VGA Registers](https://wiki.osdev.org/VGA_Registers)
- [VESA BIOS Extensions](https://en.wikipedia.org/wiki/VESA_BIOS_Extensions)
- [Windows GDI](https://docs.microsoft.com/en-us/windows/win32/gdi/windows-gdi)
- [Qt Graphics View](https://doc.qt.io/qt-6/graphicsview.html)
