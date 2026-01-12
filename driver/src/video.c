// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Video/GDI subsystem
 *
 * Handles high-resolution graphics operations from Windows GDI.
 * The guest display driver (spcdisp.drv, sunvideo.dll) sends these
 * commands for accelerated drawing operations.
 *
 * For most operations, we just track that something changed and let
 * userspace read the framebuffer. Some operations (like surface
 * management) need kernel-side tracking.
 */

#include <linux/slab.h>
#include <linux/hashtable.h>

#include "sunpci.h"
#include "ipc.h"

/* Maximum surfaces we track */
#define MAX_SURFACES    64

/* Surface flags */
#define SURF_PRIMARY    (1 << 0)
#define SURF_OFFSCREEN  (1 << 1)
#define SURF_OVERLAY    (1 << 2)
#define SURF_VISIBLE    (1 << 3)

/*
 * GDI surface descriptor
 */
struct video_surface {
    u32 handle;
    u32 width;
    u32 height;
    u32 bpp;
    u32 pitch;
    u32 flags;
    u32 fb_offset;      /* Offset in framebuffer memory */
    bool in_use;
};

/*
 * Video subsystem state
 */
struct sunpci_video_state {
    /* Surface tracking */
    struct video_surface surfaces[MAX_SURFACES];
    u32 primary_handle;
    u32 next_handle;
    
    /* Current clip region */
    s16 clip_left;
    s16 clip_top;
    s16 clip_right;
    s16 clip_bottom;
    
    /* Color key for transparency */
    u32 src_colorkey;
    u32 dst_colorkey;
    bool colorkey_enabled;
    
    /* Statistics */
    u64 blt_count;
    u64 flip_count;
    
    /* Parent device */
    struct sunpci_device *dev;
};

/*
 * Find surface by handle
 */
static struct video_surface *find_surface(struct sunpci_video_state *video, u32 handle)
{
    int i;
    
    for (i = 0; i < MAX_SURFACES; i++) {
        if (video->surfaces[i].in_use && video->surfaces[i].handle == handle)
            return &video->surfaces[i];
    }
    return NULL;
}

/*
 * Allocate new surface slot
 */
static struct video_surface *alloc_surface(struct sunpci_video_state *video)
{
    int i;
    
    for (i = 0; i < MAX_SURFACES; i++) {
        if (!video->surfaces[i].in_use) {
            video->surfaces[i].in_use = true;
            video->surfaces[i].handle = ++video->next_handle;
            return &video->surfaces[i];
        }
    }
    return NULL;
}

/*
 * Initialize video subsystem
 */
int sunpci_video_init(struct sunpci_device *dev)
{
    struct sunpci_video_state *video;
    
    video = kzalloc(sizeof(*video), GFP_KERNEL);
    if (!video)
        return -ENOMEM;
    
    video->dev = dev;
    video->next_handle = 0x1000;  /* Start handles above this */
    
    /* Default clip to full screen */
    video->clip_right = 1024;
    video->clip_bottom = 768;
    
    dev->video_state = video;
    
    pr_info("sunpci: video subsystem initialized\n");
    return 0;
}

/*
 * Shutdown video subsystem
 */
void sunpci_video_shutdown(struct sunpci_device *dev)
{
    struct sunpci_video_state *video = dev->video_state;
    
    if (!video)
        return;
    
    pr_info("sunpci: video shutdown (blts=%llu, flips=%llu)\n",
            video->blt_count, video->flip_count);
    
    kfree(video);
    dev->video_state = NULL;
}

/*
 * Handle VIDEO_CMD_CREATE_SURF
 */
static int video_handle_create_surface(struct sunpci_device *dev,
                                       const void *payload, size_t len,
                                       void *response, size_t *rsp_len)
{
    struct sunpci_video_state *video = dev->video_state;
    const struct sunpci_video_surface *req = payload;
    struct video_surface *surf;
    
    if (len < sizeof(*req))
        return -EINVAL;
    
    surf = alloc_surface(video);
    if (!surf)
        return -ENOSPC;
    
    surf->width = le32_to_cpu(req->width);
    surf->height = le32_to_cpu(req->height);
    surf->bpp = le32_to_cpu(req->bpp);
    surf->pitch = le32_to_cpu(req->pitch);
    surf->flags = le32_to_cpu(req->flags);
    surf->fb_offset = le32_to_cpu(req->fb_offset);
    
    if (surf->flags & SURF_FLAG_PRIMARY)
        video->primary_handle = surf->handle;
    
    pr_debug("sunpci: created surface %u (%ux%u %ubpp)\n",
             surf->handle, surf->width, surf->height, surf->bpp);
    
    /* Return handle */
    if (response && *rsp_len >= 4) {
        *(u32 *)response = cpu_to_le32(surf->handle);
        *rsp_len = 4;
    }
    
    return 0;
}

/*
 * Handle VIDEO_CMD_DESTROY_SURF
 */
static int video_handle_destroy_surface(struct sunpci_device *dev,
                                        const void *payload, size_t len)
{
    struct sunpci_video_state *video = dev->video_state;
    u32 handle;
    struct video_surface *surf;
    
    if (len < 4)
        return -EINVAL;
    
    handle = le32_to_cpu(*(const u32 *)payload);
    surf = find_surface(video, handle);
    if (!surf)
        return -ENOENT;
    
    pr_debug("sunpci: destroyed surface %u\n", handle);
    
    if (video->primary_handle == handle)
        video->primary_handle = 0;
    
    memset(surf, 0, sizeof(*surf));
    return 0;
}

/*
 * Handle VIDEO_CMD_BLT
 */
static int video_handle_blt(struct sunpci_device *dev,
                            const void *payload, size_t len)
{
    struct sunpci_video_state *video = dev->video_state;
    const struct sunpci_video_blt *blt = payload;
    u16 dst_x, dst_y, w, h;
    
    if (len < sizeof(*blt))
        return -EINVAL;
    
    video->blt_count++;
    
    /* If destination is primary surface, mark as dirty */
    if (le32_to_cpu(blt->dst_handle) == video->primary_handle ||
        le32_to_cpu(blt->dst_handle) == 0) {
        dst_x = le16_to_cpu(blt->dst_x);
        dst_y = le16_to_cpu(blt->dst_y);
        w = le16_to_cpu(blt->width);
        h = le16_to_cpu(blt->height);
        
        /* Mark VGA dirty region - function declared in sunpci.h */
        sunpci_vga_mark_dirty_region(dev, dst_x, dst_y, w, h);
    }
    
    return 0;
}

/*
 * Handle VIDEO_CMD_FLIP
 */
static int video_handle_flip(struct sunpci_device *dev,
                             const void *payload, size_t len)
{
    struct sunpci_video_state *video = dev->video_state;
    u16 w, h;
    
    video->flip_count++;
    
    /* Page flip means entire screen changed */
    w = dev->display.info.width ?: 640;
    h = dev->display.info.height ?: 480;
    
    sunpci_vga_mark_dirty_region(dev, 0, 0, w, h);
    
    pr_debug("sunpci: page flip\n");
    return 0;
}

/*
 * Handle VIDEO_CMD_SET_COLORKEY
 */
static int video_handle_set_colorkey(struct sunpci_device *dev,
                                     const void *payload, size_t len)
{
    struct sunpci_video_state *video = dev->video_state;
    const struct {
        __le32 src_key;
        __le32 dst_key;
        __le32 flags;
    } __packed *ck = payload;
    
    if (len < sizeof(*ck))
        return -EINVAL;
    
    video->src_colorkey = le32_to_cpu(ck->src_key);
    video->dst_colorkey = le32_to_cpu(ck->dst_key);
    video->colorkey_enabled = le32_to_cpu(ck->flags) != 0;
    
    return 0;
}

/*
 * Handle VIDEO_CMD_SET_CLIPLIST
 */
static int video_handle_set_cliplist(struct sunpci_device *dev,
                                     const void *payload, size_t len)
{
    struct sunpci_video_state *video = dev->video_state;
    const struct {
        __le16 left;
        __le16 top;
        __le16 right;
        __le16 bottom;
    } __packed *clip = payload;
    
    if (len < sizeof(*clip))
        return -EINVAL;
    
    video->clip_left = le16_to_cpu(clip->left);
    video->clip_top = le16_to_cpu(clip->top);
    video->clip_right = le16_to_cpu(clip->right);
    video->clip_bottom = le16_to_cpu(clip->bottom);
    
    return 0;
}

/*
 * Handle VIDEO_CMD_LOCK / VIDEO_CMD_UNLOCK
 * These are used when guest wants direct framebuffer access
 */
static int video_handle_lock(struct sunpci_device *dev,
                             const void *payload, size_t len,
                             void *response, size_t *rsp_len)
{
    /* For now, just return success - we don't need to do anything
     * since the guest already has access to video memory */
    
    if (response && *rsp_len >= 4) {
        /* Return framebuffer pointer (guest-side address) */
        *(u32 *)response = 0;  /* Guest fills this in */
        *rsp_len = 4;
    }
    
    return 0;
}

/*
 * Main video message dispatcher
 */
int sunpci_video_handle_message(struct sunpci_device *dev,
                                u16 command,
                                const void *payload, size_t len,
                                void *response, size_t *rsp_len)
{
    struct sunpci_video_state *video = dev->video_state;
    int ret = 0;
    
    if (!video)
        return -ENODEV;
    
    switch (command) {
    case VIDEO_CMD_CREATE_SURF:
        ret = video_handle_create_surface(dev, payload, len, response, rsp_len);
        break;
        
    case VIDEO_CMD_DESTROY_SURF:
        ret = video_handle_destroy_surface(dev, payload, len);
        break;
        
    case VIDEO_CMD_LOCK:
        ret = video_handle_lock(dev, payload, len, response, rsp_len);
        break;
        
    case VIDEO_CMD_UNLOCK:
        /* Nothing to do */
        break;
        
    case VIDEO_CMD_BLT:
        ret = video_handle_blt(dev, payload, len);
        break;
        
    case VIDEO_CMD_FLIP:
        ret = video_handle_flip(dev, payload, len);
        break;
        
    case VIDEO_CMD_SET_COLORKEY:
        ret = video_handle_set_colorkey(dev, payload, len);
        break;
        
    case VIDEO_CMD_SET_CLIPLIST:
        ret = video_handle_set_cliplist(dev, payload, len);
        break;
        
    default:
        pr_debug("sunpci: unknown video command 0x%04x\n", command);
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

