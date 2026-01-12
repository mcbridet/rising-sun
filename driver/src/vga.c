/*
 * SunPCI driver - VGA display subsystem
 *
 * Handles VGA text mode, palette management, mode switching, and
 * dirty rectangle tracking for framebuffer updates.
 *
 * The guest sends VGA_CMD_* messages when:
 *   - Video mode changes (text/graphics, resolution)
 *   - Palette is modified
 *   - Cursor position/shape changes
 *   - Framebuffer regions are updated (dirty rects)
 */

#include <linux/slab.h>
#include <linux/uaccess.h>

#include "sunpci.h"
#include "ipc.h"
#include "regs.h"

/* VGA text mode character cell */
struct vga_char {
    u8 character;
    u8 attribute;
} __packed;

/* Standard VGA palette (16 colors) */
static const u32 vga_default_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/*
 * VGA state structure
 */
struct sunpci_vga_state {
    /* Current mode */
    u8 mode;
    bool graphics_mode;
    
    /* Text mode state */
    u8 text_cols;
    u8 text_rows;
    u16 cursor_pos;
    u8 cursor_start;
    u8 cursor_end;
    bool cursor_visible;
    
    /* Graphics mode state */
    u16 width;
    u16 height;
    u8 bpp;
    u32 pitch;
    
    /* Palette */
    u32 palette[256];
    
    /* Dirty tracking */
    bool dirty;
    u16 dirty_x;
    u16 dirty_y;
    u16 dirty_w;
    u16 dirty_h;
    spinlock_t dirty_lock;
    
    /* Text buffer shadow (for text mode) */
    struct vga_char *text_buffer;
    size_t text_buffer_size;
};

/*
 * Initialize VGA subsystem
 */
int sunpci_vga_init(struct sunpci_device *dev)
{
    struct sunpci_vga_state *vga;
    int i;
    
    vga = kzalloc(sizeof(*vga), GFP_KERNEL);
    if (!vga)
        return -ENOMEM;
    
    spin_lock_init(&vga->dirty_lock);
    
    /* Default to 80x25 text mode */
    vga->mode = 0x03;
    vga->graphics_mode = false;
    vga->text_cols = 80;
    vga->text_rows = 25;
    vga->cursor_visible = true;
    vga->cursor_start = 14;
    vga->cursor_end = 15;
    
    /* Default graphics (for when we switch) */
    vga->width = 640;
    vga->height = 480;
    vga->bpp = 8;
    
    /* Initialize default palette */
    for (i = 0; i < 16; i++)
        vga->palette[i] = vga_default_palette[i];
    for (i = 16; i < 256; i++)
        vga->palette[i] = 0;  /* Will be set by guest */
    
    /* Allocate text buffer */
    vga->text_buffer_size = 80 * 25 * sizeof(struct vga_char);
    vga->text_buffer = kzalloc(vga->text_buffer_size, GFP_KERNEL);
    if (!vga->text_buffer) {
        kfree(vga);
        return -ENOMEM;
    }
    
    dev->vga_state = vga;
    
    pr_info("sunpci: VGA initialized (80x25 text mode)\n");
    return 0;
}

/*
 * Shutdown VGA subsystem
 */
void sunpci_vga_shutdown(struct sunpci_device *dev)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    
    if (!vga)
        return;
    
    kfree(vga->text_buffer);
    kfree(vga);
    dev->vga_state = NULL;
}

/*
 * Mark region as dirty
 */
static void vga_mark_dirty(struct sunpci_vga_state *vga,
                           u16 x, u16 y, u16 w, u16 h)
{
    unsigned long flags;
    
    spin_lock_irqsave(&vga->dirty_lock, flags);
    
    if (!vga->dirty) {
        /* First dirty region */
        vga->dirty_x = x;
        vga->dirty_y = y;
        vga->dirty_w = w;
        vga->dirty_h = h;
    } else {
        /* Expand dirty region to include new area */
        u16 x2 = max(vga->dirty_x + vga->dirty_w, x + w);
        u16 y2 = max(vga->dirty_y + vga->dirty_h, y + h);
        vga->dirty_x = min(vga->dirty_x, x);
        vga->dirty_y = min(vga->dirty_y, y);
        vga->dirty_w = x2 - vga->dirty_x;
        vga->dirty_h = y2 - vga->dirty_y;
    }
    vga->dirty = true;
    
    spin_unlock_irqrestore(&vga->dirty_lock, flags);
}

/*
 * Get and clear dirty region
 */
bool sunpci_vga_get_dirty(struct sunpci_device *dev,
                          u16 *x, u16 *y, u16 *w, u16 *h)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    unsigned long flags;
    bool was_dirty;
    
    if (!vga)
        return false;
    
    spin_lock_irqsave(&vga->dirty_lock, flags);
    
    was_dirty = vga->dirty;
    if (was_dirty) {
        *x = vga->dirty_x;
        *y = vga->dirty_y;
        *w = vga->dirty_w;
        *h = vga->dirty_h;
        vga->dirty = false;
    }
    
    spin_unlock_irqrestore(&vga->dirty_lock, flags);
    return was_dirty;
}

/*
 * Handle VGA_CMD_SET_MODE
 */
static int vga_handle_set_mode(struct sunpci_device *dev,
                               const void *payload, size_t len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    const struct sunpci_vga_mode *mode = payload;
    
    if (len < sizeof(*mode))
        return -EINVAL;
    
    vga->width = le16_to_cpu(mode->width);
    vga->height = le16_to_cpu(mode->height);
    vga->bpp = le16_to_cpu(mode->bpp);
    vga->pitch = le32_to_cpu(mode->pitch);
    
    /* Determine if graphics or text mode */
    if (vga->width == 720 && vga->height == 400 && vga->bpp == 4) {
        /* Text mode (80x25, 9x16 font = 720x400) */
        vga->graphics_mode = false;
        vga->text_cols = 80;
        vga->text_rows = 25;
    } else if (vga->width == 640 && vga->height == 200) {
        /* CGA text mode */
        vga->graphics_mode = false;
        vga->text_cols = 80;
        vga->text_rows = 25;
    } else {
        vga->graphics_mode = true;
    }
    
    /* Update display info for userspace */
    dev->display.info.width = vga->width;
    dev->display.info.height = vga->height;
    dev->display.info.color_depth = vga->bpp;
    dev->display.info.mode = vga->graphics_mode ? 
                             SUNPCI_DISPLAY_MODE_GRAPHICS : 
                             SUNPCI_DISPLAY_MODE_TEXT;
    dev->display.info.text_cols = vga->text_cols;
    dev->display.info.text_rows = vga->text_rows;
    
    /* Mark entire screen dirty */
    vga_mark_dirty(vga, 0, 0, vga->width, vga->height);
    
    pr_info("sunpci: VGA mode set: %dx%d %dbpp %s\n",
            vga->width, vga->height, vga->bpp,
            vga->graphics_mode ? "graphics" : "text");
    
    return 0;
}

/*
 * Handle VGA_CMD_SET_PALETTE
 */
static int vga_handle_set_palette(struct sunpci_device *dev,
                                  const void *payload, size_t len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    const u8 *data = payload;
    size_t count, i;
    u8 start_index;
    
    if (len < 1)
        return -EINVAL;
    
    start_index = data[0];
    data++;
    len--;
    
    /* Each palette entry is 3 bytes (R, G, B) */
    count = len / 3;
    if (start_index + count > 256)
        count = 256 - start_index;
    
    for (i = 0; i < count; i++) {
        u8 r = data[i * 3 + 0];
        u8 g = data[i * 3 + 1];
        u8 b = data[i * 3 + 2];
        /* VGA palette is 6-bit, scale to 8-bit */
        vga->palette[start_index + i] = 
            ((r << 2) << 16) | ((g << 2) << 8) | (b << 2);
    }
    
    /* Palette change means screen needs redraw */
    vga_mark_dirty(vga, 0, 0, vga->width, vga->height);
    
    return 0;
}

/*
 * Handle VGA_CMD_DIRTY_RECT
 */
static int vga_handle_dirty_rect(struct sunpci_device *dev,
                                 const void *payload, size_t len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    const struct sunpci_vga_dirty *dirty = payload;
    
    if (len < sizeof(*dirty))
        return -EINVAL;
    
    vga_mark_dirty(vga, 
                   le16_to_cpu(dirty->x),
                   le16_to_cpu(dirty->y),
                   le16_to_cpu(dirty->width),
                   le16_to_cpu(dirty->height));
    
    return 0;
}

/*
 * Handle VGA_CMD_CURSOR_POS
 */
static int vga_handle_cursor_pos(struct sunpci_device *dev,
                                 const void *payload, size_t len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    const struct {
        __le16 x;
        __le16 y;
    } __packed *pos = payload;
    u16 old_pos, new_pos;
    
    if (len < sizeof(*pos))
        return -EINVAL;
    
    old_pos = vga->cursor_pos;
    new_pos = le16_to_cpu(pos->y) * vga->text_cols + le16_to_cpu(pos->x);
    vga->cursor_pos = new_pos;
    
    /* Mark old and new cursor positions dirty */
    if (!vga->graphics_mode && vga->cursor_visible) {
        u16 old_x = old_pos % vga->text_cols;
        u16 old_y = old_pos / vga->text_cols;
        u16 new_x = le16_to_cpu(pos->x);
        u16 new_y = le16_to_cpu(pos->y);
        
        /* Convert to pixel coordinates (assuming 8x16 font) */
        vga_mark_dirty(vga, old_x * 8, old_y * 16, 8, 16);
        vga_mark_dirty(vga, new_x * 8, new_y * 16, 8, 16);
    }
    
    return 0;
}

/*
 * Handle VGA_CMD_CURSOR_SHAPE
 */
static int vga_handle_cursor_shape(struct sunpci_device *dev,
                                   const void *payload, size_t len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    const struct {
        u8 start;
        u8 end;
        u8 visible;
    } __packed *shape = payload;
    
    if (len < sizeof(*shape))
        return -EINVAL;
    
    vga->cursor_start = shape->start;
    vga->cursor_end = shape->end;
    vga->cursor_visible = shape->visible != 0;
    
    return 0;
}

/*
 * Main VGA message dispatcher
 */
int sunpci_vga_handle_message(struct sunpci_device *dev,
                              u16 command,
                              const void *payload, size_t len,
                              void *response, size_t *rsp_len)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    int ret = 0;
    
    if (!vga)
        return -ENODEV;
    
    switch (command) {
    case VGA_CMD_SET_MODE:
        ret = vga_handle_set_mode(dev, payload, len);
        break;
        
    case VGA_CMD_GET_MODE:
        /* Return current mode info */
        if (response && *rsp_len >= sizeof(struct sunpci_vga_mode)) {
            struct sunpci_vga_mode *mode = response;
            mode->width = cpu_to_le16(vga->width);
            mode->height = cpu_to_le16(vga->height);
            mode->bpp = cpu_to_le16(vga->bpp);
            mode->flags = cpu_to_le16(vga->graphics_mode ? 1 : 0);
            mode->pitch = cpu_to_le32(vga->pitch);
            mode->fb_offset = 0;
            *rsp_len = sizeof(*mode);
        }
        break;
        
    case VGA_CMD_SET_PALETTE:
        ret = vga_handle_set_palette(dev, payload, len);
        break;
        
    case VGA_CMD_GET_PALETTE:
        /* Return palette data */
        if (response && *rsp_len >= 256 * 4) {
            memcpy(response, vga->palette, 256 * 4);
            *rsp_len = 256 * 4;
        }
        break;
        
    case VGA_CMD_DIRTY_RECT:
        ret = vga_handle_dirty_rect(dev, payload, len);
        break;
        
    case VGA_CMD_CURSOR_POS:
        ret = vga_handle_cursor_pos(dev, payload, len);
        break;
        
    case VGA_CMD_CURSOR_SHAPE:
        ret = vga_handle_cursor_shape(dev, payload, len);
        break;
        
    default:
        pr_debug("sunpci: unknown VGA command 0x%04x\n", command);
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

/*
 * Get current VGA info for ioctl
 */
int sunpci_vga_get_info(struct sunpci_device *dev,
                        struct sunpci_display_info *info)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    
    if (!vga) {
        memset(info, 0, sizeof(*info));
        return -ENODEV;
    }
    
    info->width = vga->width;
    info->height = vga->height;
    info->color_depth = vga->bpp;
    info->mode = vga->graphics_mode ? 
                 SUNPCI_DISPLAY_MODE_GRAPHICS : 
                 SUNPCI_DISPLAY_MODE_TEXT;
    info->text_cols = vga->text_cols;
    info->text_rows = vga->text_rows;
    
    return 0;
}

/*
 * Get palette for userspace
 */
int sunpci_vga_get_palette(struct sunpci_device *dev, u32 *palette, size_t count)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    
    if (!vga)
        return -ENODEV;
    
    if (count > 256)
        count = 256;
    
    memcpy(palette, vga->palette, count * sizeof(u32));
    return 0;
}

/*
 * Mark dirty region from external callers (video.c BitBlt/Flip)
 * This is the exported wrapper around the static vga_mark_dirty()
 */
void sunpci_vga_mark_dirty_region(struct sunpci_device *dev,
                                  u16 x, u16 y, u16 w, u16 h)
{
    struct sunpci_vga_state *vga = dev->vga_state;
    
    if (vga)
        vga_mark_dirty(vga, x, y, w, h);
}
