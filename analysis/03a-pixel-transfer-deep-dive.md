# SunPCI Pixel Transfer Deep Dive

## Executive Summary

The SunPCI does **NOT** expose a traditional framebuffer to the guest. Instead, it uses a
**command-based protocol** where the Windows display driver sends GDI-like drawing commands
through a queue-based IPC mechanism to the host, which renders them using X11/Xlib.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              GUEST (x86 on SunPCI card)                     │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        Windows 95/98/NT                              │   │
│  │  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────────┐  │   │
│  │  │   App/GDI   │───▶│ spcdisp.drv │───▶│    sunpci.vxd           │  │   │
│  │  │ (drawing)   │    │ (display)   │    │ (IPC to host)           │  │   │
│  │  └─────────────┘    └─────────────┘    └───────────┬─────────────┘  │   │
│  └─────────────────────────────────────────────────────┼────────────────┘   │
│                                                        │                    │
│                                                        ▼                    │
│                              ┌─────────────────────────────────┐            │
│                              │   Shared Memory / Ring Buffer   │            │
│                              │   (on SunPCI card, PCI BAR)     │            │
│                              └───────────────┬─────────────────┘            │
└──────────────────────────────────────────────┼──────────────────────────────┘
                                               │ PCI Bus
┌──────────────────────────────────────────────┼──────────────────────────────┐
│                              HOST (SPARC/Solaris)                           │
│                              ┌───────────────┴─────────────────┐            │
│                              │   Shared Memory / Ring Buffer   │            │
│                              │   (mapped via PCI BAR)          │            │
│                              └───────────────┬─────────────────┘            │
│                                              │                              │
│  ┌───────────────────────────────────────────┼──────────────────────────┐   │
│  │                    sunpcidrv (kernel)     │                          │   │
│  │  ┌─────────────┐    ┌─────────────────────┴───────────────────────┐  │   │
│  │  │  ipc_read   │◀──▶│  Q_Read / Q_Write (ring buffer primitives) │  │   │
│  │  │  ipc_write  │    │  pmipc_read / pmipc_write (port-mapped IPC)│  │   │
│  │  └──────┬──────┘    └─────────────────────────────────────────────┘  │   │
│  └─────────┼────────────────────────────────────────────────────────────┘   │
│            │ read()/write() syscalls                                        │
│            ▼                                                                │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                       sunpcbinary (userspace)                         │   │
│  │                                                                       │   │
│  │   ┌─────────────────┐     ┌─────────────────────────────────────┐    │   │
│  │   │ CoreDispatcher  │────▶│ VidDispatcher (libvideo.so)        │    │   │
│  │   │ (message router)│     │   ├─ ProcBitBlt                    │    │   │
│  │   └─────────────────┘     │   ├─ ProcSetBitmapBits             │    │   │
│  │                           │   ├─ ProcTextOut                   │    │   │
│  │                           │   ├─ ProcRectangles                │    │   │
│  │                           │   └─ ... (30+ operations)          │    │   │
│  │                           └─────────────────┬───────────────────┘    │   │
│  │                                             │                        │   │
│  │   ┌─────────────────┐     ┌─────────────────┴───────────────────┐    │   │
│  │   │ VGADispatcher   │────▶│ ScreenSurface / DrawingSurface     │    │   │
│  │   │ (libvga.so)     │     │   ├─ XCreateImage                  │    │   │
│  │   │ (text modes)    │     │   ├─ XPutImage                     │    │   │
│  │   └─────────────────┘     │   └─ XFillRectangle, etc.          │    │   │
│  │                           └─────────────────┬───────────────────┘    │   │
│  └─────────────────────────────────────────────┼────────────────────────┘   │
│                                                │                            │
│                                                ▼                            │
│                                    ┌───────────────────────┐                │
│                                    │    X11 Server         │                │
│                                    │    (OpenWindows)      │                │
│                                    └───────────────────────┘                │
└─────────────────────────────────────────────────────────────────────────────┘
```

## IPC Mechanism

### Ring Buffer Queues

The kernel driver (`sunpcidrv`) implements ring buffer queues for bidirectional IPC:

| Function | Purpose |
|----------|---------|
| `Q_Add` | Add data to a queue |
| `Q_Read` | Read data from a queue |
| `Q_Write` | Write data to a queue |
| `Q_ReadDataAvail` | Check if data is available |
| `Q_WriteSpaceAvail` | Check if space is available |

### IPC Channels

Multiple IPC channels exist for different purposes:

| Function | Purpose |
|----------|---------|
| `ipc_read` / `ipc_write` | General IPC |
| `pmipc_read` / `pmipc_write` | Port-mapped IPC (for display?) |
| `rmipc_read` / `rmipc_write` | Register-mapped IPC |
| `kmem_queue_read` | Kernel memory queue |

### Memory Architecture

```c
// PCI BARs visible in VxD strings:
// mem0: Base address / size (hw address)
// mem1: Base address / size  
// mem2: Base address / size

// The shared memory region contains:
// - Command ring buffer (guest → host)
// - Response ring buffer (host → guest)
// - Bulk data transfer area (for bitmaps)
```

## Video Protocol

### Message Format

```c
struct VideoProtocolMessage {
    uint32_t dispatcher_id;   // Which dispatcher handles this (VGA=1, Video=2, etc.)
    uint32_t opcode;          // Operation code
    uint32_t length;          // Payload length
    uint8_t  payload[];       // Variable-length data
};
```

### Dispatch Tables

Three dispatch tables exist in `libvideo.so`:

| Table | Address | Purpose |
|-------|---------|---------|
| `NativeDispatchTable` | 0x2fe54 | SPARC native byte order |
| `SwappedDispatchTable` | 0x2ff18 | x86 byte order (swapped) |
| `DebugDispatchTable` | 0x2ffdc | Debug/verbose mode |

### Video Opcodes (Reconstructed)

Based on the dispatch table relocations:

| Opcode | Function | Purpose |
|--------|----------|---------|
| 0-9 | `ProcNoop` | No-operation (padding) |
| 10 | `ProcEnableScreen` | Enable display output |
| 11 | `ProcDisableScreen` | Disable display output |
| 12 | `ProcSetPalette` | Set color palette (256 entries) |
| 13 | `ProcSetPaletteTrans` | Set palette translation table |
| 14 | `ProcCreatePen` | Create GDI pen object |
| 15 | `ProcCreateBrush` | Create GDI brush object |
| 16 | `ProcCreateSurface` | Create drawing surface |
| 17 | `ProcCreateFont` | Create GDI font object |
| 18 | `ProcDownloadGlyphs` | Download font glyphs |
| 19 | `ProcDownloadGlyphsNT` | Download glyphs (NT variant) |
| 20 | `ProcDeleteObject` | Delete GDI object |
| 21 | `ProcGetBitmapBits` | Read bitmap data |
| 22 | `ProcSetBitmapBits` | Write bitmap data |
| 23 | `ProcSetMonoBitmapBits` | Write mono bitmap |
| 24 | `ProcGetMonoBitmapBits` | Read mono bitmap |
| 25 | `ProcSetDIBitsToDevice` | DIB to device transfer |
| 26 | `ProcSetDeviceBitmapBits` | Device bitmap data |
| 27 | `ProcDecompressDeviceBitmapBits` | RLE decompression |
| 28 | `ProcSetPixel` | Set single pixel |
| 29 | `ProcGetPixel` | Get single pixel |
| 30 | `ProcPolygonOld` | Draw polygon (legacy) |
| 31 | `ProcPolygon` | Draw polygon |
| 32 | `ProcPolySpansOld` | Draw spans (legacy) |
| 33 | `ProcPolySpans` | Draw polygon spans |
| 34 | `ProcPolyline` | Draw polyline |
| 35 | `ProcRectanglesOld` | Draw rectangles (legacy) |
| 36 | `ProcRectangles` | Draw rectangles |
| 37 | `ProcSetClipRects` | Set clipping region |
| 38 | `ProcBitBlt` | Bit block transfer (THE key operation) |
| 39 | `ProcTextOut` | Draw text (Win95) |
| 40 | `ProcTextOutNT` | Draw text (WinNT) |
| 41 | `ProcSaveScreenRect` | Save screen region |
| 42 | `ProcFastBorder` | Fast border drawing |
| 43 | `ProcFlush` | Flush pending operations |
| 44 | `ProcScanLR` | Scanline operations |
| 45 | `ProcSetCursor` | Set mouse cursor |
| 46 | `ProcGetGDIInfo` | Get GDI capabilities |
| 47 | `ProcGetDepthInfo` | Get color depth info |
| 48-49 | ? | Unknown |
| 50 | `ProcDumpFont` | Debug: dump font info |

## Pixel Data Flow

### Case 1: GDI BitBlt (Most Common)

```
1. App calls BitBlt() in Windows
2. GDI calls spcdisp.drv's BitBlt entry point
3. spcdisp.drv formats a ProcBitBlt message:
   - Source surface ID
   - Destination surface ID  
   - Source rectangle
   - Destination point
   - Raster operation (ROP)
4. Message written to shared memory ring buffer via sunpci.vxd
5. Interrupt/poll triggers host to read
6. sunpcbinary reads via kernel driver
7. VidDispatcher routes to DBitBlt
8. DBitBlt calls XCopyArea or similar X11 function
9. X11 server renders to display
```

### Case 2: Bitmap Transfer

```
1. App creates/modifies a bitmap
2. Windows calls SetDIBitsToDevice or SetBitmapBits
3. spcdisp.drv formats a ProcSetBitmapBits message:
   - Surface ID
   - Width, height, format
   - Actual pixel data (possibly compressed)
4. Large data transferred via bulk transfer area
5. Host receives and calls DrawingSurfaceSetBitmapBits8 or SetBitmapBits24
6. Creates/updates XImage
7. XPutImage to display
```

### Case 3: VGA Mode 13h (DOS Games)

```
1. DOS game sets mode 13h via INT 10h
2. VGA BIOS (emulated) calls DSetVideoMode
3. Game writes directly to A000:0000 (video memory)
4. Periodically (SMI tick or polling):
   - VGA registers/memory state captured
   - DSetVideoData sends 64KB framebuffer
5. Host's VGATextMgr converts:
   - VGATextMgrDraw8BitBitmap for 256-color
   - Palette lookup via VGA DAC registers
6. XPutImage to display
```

### Case 4: Text Mode

```
1. DOS/BIOS writes to B800:0000 (text video memory)
2. VGA text manager tracks character+attribute pairs
3. DSetVideoData sends text buffer
4. VGATextMgrSetCharacters processes
5. XDrawImageString with PC font (pc8x16s)
```

## Color Conversion

### DIB Converters

| Converter | Purpose |
|-----------|---------|
| `DIBConvert1` | 1-bit monochrome |
| `DIBConvert4` | 4-bit (16 colors) |
| `DIBConvert8` | 8-bit (256 colors) with palette |
| `DIBConvert24` | 24-bit truecolor |

### Internal Converters

| Function | Purpose |
|----------|---------|
| `Convert1to1` | Mono → Mono |
| `Convert4to8` | 16 → 256 colors |
| `Convert4to24` | 16 → TrueColor |
| `Convert8to8` | 256 → 256 (palette remap) |
| `Convert8to24` | 256 → TrueColor |
| `Convert24to8` | TrueColor → 256 (dithering) |
| `Convert24to24` | TrueColor copy |

### Dithering

- `dither16` - 16-color dithering table
- `dither256` - 256-color dithering table
- `defaultWinColors16` - Standard Windows 16-color palette
- `defaultWinColors256` - Standard Windows 256-color palette

## Why Direct Framebuffer Access Doesn't Work

### The Problem

The SunPCI card has x86 CPU + RAM on the card itself. When a DOS game writes to
`0xA0000` (VGA video memory), that write happens **on the card**, not visible
to the host SPARC CPU.

### What Would Be Needed

For real framebuffer access to work, one of these would be required:

1. **Hardware trapping**: The card would need to trap writes to the VGA memory
   range and forward them to the host. This isn't implemented.

2. **Periodic sync**: The VGA memory could be periodically copied to the host.
   The SMI tick (`DSMITick`) suggests this might happen for Mode 13h, but
   it would be slow and laggy.

3. **Shared memory framebuffer**: The card could expose its video memory
   directly to the host via PCI BAR. This would require the guest to know
   about this special arrangement.

### What Actually Happens

- **GDI apps**: Work because they go through the display driver
- **Mode 13h DOS**: Works (slowly) via periodic sync
- **DirectDraw**: Fails - bypasses display driver, writes to "framebuffer" that doesn't exist
- **Mode-X**: Probably fails - uses VGA tricks the sync might not handle

## Performance Characteristics

| Operation | Latency | Throughput |
|-----------|---------|------------|
| Small GDI op (rect fill) | ~1ms | High (command-based) |
| BitBlt (on-screen) | ~2-5ms | Medium |
| Large bitmap transfer | ~10-50ms | Low (data must cross PCI) |
| Full screen update (640x480x8) | ~50-100ms | ~3-6 MB/s |
| Mode 13h frame | ~50-100ms | ~10-20 FPS max |

## Protocol Message Structures (Reconstructed)

Based on disassembly and debug strings, here are the key message formats:

### Common Header

```c
// All messages start with a 4-byte header
struct VideoMessageHeader {
    uint8_t  dispatcher_id;    // 0=Core, 1=VGA, 2=Video, etc.
    uint8_t  opcode;           // Operation within dispatcher
    uint16_t size;             // Total message size
};
```

### SetVideoMode (VGA)

```c
// opcode: SetVideoMode
// Offsets from disassembly of ProcSetVideoMode
struct VgaSetVideoModeMsg {
    uint8_t  dispatcher_id;    // +0x00
    uint8_t  opcode;           // +0x01
    uint16_t size;             // +0x02
    uint8_t  mode_number;      // +0x04: VGA mode (0x13 = 320x200x256)
    uint8_t  color_depth;      // +0x05: bits per pixel
    uint16_t width;            // +0x06: pixels
    uint16_t height;           // +0x08: pixels
    uint16_t text_cols;        // +0x0A: text columns (for text modes)
    uint16_t text_rows;        // +0x0C: text rows
    uint8_t  is_graphics;      // +0x0E: 0=text, 1=graphics
    uint8_t  reserved;         // +0x0F
    uint16_t mode_id;          // +0x10: internal mode identifier
};
```

### SetVideoData (VGA Framebuffer)

```c
// opcode: SetVideoData  
// The actual framebuffer data for VGA modes
struct VgaSetVideoDataMsg {
    uint8_t  dispatcher_id;    // +0x00
    uint8_t  data_type;        // +0x01: 0=text, 1=8bit, 2=4bit, 3=planar
    uint16_t size;             // +0x02
    // Variable data follows based on data_type
    
    // For 8-bit graphics (Mode 13h):
    // uint8_t pixels[width * height];
    
    // For text mode:
    // struct { uint8_t char; uint8_t attr; } cells[cols * rows];
};
```

### BitBlt (Video)

```c
// opcode: 38 (BitBlt)
// The most important graphics operation
struct VideoBitBltMsg {
    uint8_t  dispatcher_id;    // +0x00
    uint8_t  opcode;           // +0x01: 38
    uint16_t size;             // +0x02
    uint16_t dst_surface_id;   // +0x04: destination surface index
    uint16_t src_surface_id;   // +0x06: source surface index (0x4000+ = screen)
    uint16_t brush_id;         // +0x08: brush index (0x800+ = pattern)
    uint8_t  has_src_rect;     // +0x0A: boolean
    uint8_t  reserved;         // +0x0B
    
    // Following data at +0x0C (varies based on flags):
    // int16_t src_rect[4];    // +0x0C: x1, y1, x2, y2
    // int16_t dst_point[2];   // varies: x, y
    
    // At fixed offsets:
    // uint16_t rop3;          // +0x20: raster operation code
    // uint8_t  bg_mode;       // +0x22
    // uint8_t  reserved;
    // uint16_t bg_color;      // +0x24
    // uint16_t fg_color;      // +0x26
    // int16_t brush_offset[2];// varies: x, y
};
```

### SetBitmapBits (Video)

```c
// opcode: 22 (SetBitmapBits)
struct VideoSetBitmapBitsMsg {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // 22
    uint16_t size;
    uint16_t surface_id;       // target surface
    uint16_t width;
    uint16_t height;
    uint16_t bits_size;        // size of pixel data
    // uint8_t pixels[bits_size];
};
```

### SetDIBitsToDevice (Video)

```c
// opcode: 25 (SetDIBitsToDevice)
struct VideoSetDIBitsMsg {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // 25
    uint16_t size;
    uint16_t surface_id;
    uint16_t width;
    uint16_t height;
    uint16_t color_size;       // palette data size
    int16_t  clip_rect[4];     // x1, y1, x2, y2
    // uint8_t color_table[color_size];
    // uint8_t pixels[...];
};
```

### TextOut (Video)

```c
// opcode: 39 (TextOut)
struct VideoTextOutMsg {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // 39
    uint16_t size;
    uint16_t surface_id;
    uint16_t font_id;
    uint16_t text_color;
    uint16_t bg_color;
    int16_t  bg_rect[4];
    int16_t  clip_rect[4];
    int16_t  opaque_rect[4];
    uint16_t n_chars;
    int16_t  y;
    // struct { uint16_t index; int16_t x; } glyphs[n_chars];
};
```

### CreateSurface (Video)

```c
// opcode: 16 (CreateSurface)
struct VideoCreateSurfaceMsg {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // 16
    uint16_t size;
    uint16_t surface_id;       // assigned ID
    uint16_t width;
    uint16_t height;
    uint8_t  depth;            // bits per pixel
    uint8_t  flags;
};
```

### SetPalette (Video/VGA)

```c
// opcode: 12 (SetPalette)
struct VideoSetPaletteMsg {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // 12
    uint16_t size;
    uint8_t  start_index;
    uint8_t  count;
    // struct { uint8_t r, g, b; } entries[count];
};
```

### Response Messages

```c
// Responses go back via the return queue
struct VideoResponse {
    uint8_t  dispatcher_id;
    uint8_t  opcode;           // echoed from request
    uint16_t status;           // 0 = success
    // Variable response data depending on opcode
};

// For GetBitmapBits response:
struct GetBitmapBitsResponse {
    uint8_t  dispatcher_id;
    uint8_t  opcode;
    uint16_t status;
    uint16_t bits_size;
    // uint8_t pixels[bits_size];
};
```

## VGA Pixel Conversion

From disassembly of `VGATextMgrDraw8BitBitmap`:

```c
// 8-bit indexed -> 16-bit via palette lookup
// The palette is stored as 16-bit RGB565 values at a fixed offset

void VGATextMgrDraw8BitBitmap(
    VGATextMgr *mgr,
    uint8_t *src_pixels,     // indexed 8-bit pixels
    int width,
    int height,
    int dst_y
) {
    XImage *image = mgr->ximage;
    uint16_t *palette = mgr->palette_rgb565;  // 256 entries
    
    // Get destination pointer
    uint16_t *dst = (uint16_t*)image->data + dst_y * image->bytes_per_line/2;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t index = *src_pixels++;
            *dst++ = palette[index];  // 16-bit lookup
        }
    }
}

// For 24-bit displays, a different path is used:
void VGATextMgrDraw8BitBitmapAs24Bit(...) {
    // Similar but outputs 24-bit RGB
    uint32_t *palette_rgb = mgr->palette_rgb888;
    // ...
}
```

## Implications for Rising Sun Implementation

### What We Must Implement

1. **Ring buffer IPC**: Bidirectional command/response queues
2. **Video protocol parser**: Handle all ~50 opcodes
3. **GDI object management**: Surfaces, pens, brushes, fonts
4. **Bitmap converters**: All color depth conversions
5. **VGA text mode**: Font rendering with PC font
6. **VGA graphics modes**: Mode 13h frame sync

### What We Can Improve

1. **Shared memory framebuffer**: Use mmapped buffer for faster updates
2. **DRM/KMS**: Direct rendering instead of X11
3. **Vulkan/OpenGL**: Hardware-accelerated scaling
4. **Better Mode 13h**: More frequent sync, or trap writes with KVM

### What We Can't Improve

1. **DirectDraw games**: Would need different guest drivers
2. **Latency**: Fundamental to the architecture
3. **Direct hardware access**: Card architecture limitation
