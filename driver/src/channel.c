/*
 * SunPCi Named Channel Support
 *
 * Implements the NT bridge.sys channel API for Windows NT/2000 drivers.
 * Channels provide a named abstraction over the dispatcher system.
 *
 * Key channels:
 *   - "NewInt13Dispatcher" -> SUNPCI_DISP_STORAGE (disk I/O)
 *   - "VGADispatcher" -> SUNPCI_DISP_VGA
 *   - "NetworkDispatcher" -> SUNPCI_DISP_NETWORK
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>

#include "sunpci.h"
#include "ipc.h"

/* Maximum number of active channels */
#define MAX_CHANNELS    16

/* Channel state */
struct sunpci_channel {
    u32 id;                         /* Channel handle */
    u32 dispatcher;                 /* Mapped dispatcher ID */
    u32 flags;                      /* CHANNEL_FLAG_* */
    bool active;                    /* Channel in use */
    char name[SUNPCI_CHANNEL_NAME_MAX + 1];  /* ASCII name */
};

/* Channel registry for a device */
struct sunpci_channel_registry {
    struct mutex lock;
    u32 next_id;
    struct sunpci_channel channels[MAX_CHANNELS];
};

/* Well-known channel mappings */
static const struct {
    const char *name;
    u32 dispatcher;
} known_channels[] = {
    { CHANNEL_NAME_INT13,      SUNPCI_DISP_STORAGE },
    { CHANNEL_NAME_VGA,        SUNPCI_DISP_VGA },
    { CHANNEL_NAME_VIDEO,      SUNPCI_DISP_VIDEO },
    { CHANNEL_NAME_NETWORK,    SUNPCI_DISP_NETWORK },
    { CHANNEL_NAME_FSD,        SUNPCI_DISP_FSD },
    { CHANNEL_NAME_CLIPBOARD,  SUNPCI_DISP_CLIP },
    { NULL, 0 }
};

/*
 * Convert UTF-16LE to ASCII (simple conversion for channel names)
 */
static int utf16le_to_ascii(const __le16 *src, u32 len_bytes, char *dst, size_t dst_size)
{
    u32 i;
    u32 num_chars = len_bytes / 2;
    
    if (num_chars >= dst_size)
        num_chars = dst_size - 1;
    
    for (i = 0; i < num_chars; i++) {
        u16 ch = le16_to_cpu(src[i]);
        if (ch == 0)
            break;
        if (ch > 127)
            dst[i] = '?';  /* Non-ASCII */
        else
            dst[i] = (char)ch;
    }
    dst[i] = '\0';
    
    return i;
}

/*
 * Look up dispatcher ID for a channel name
 */
static int channel_name_to_dispatcher(const char *name)
{
    int i;
    
    for (i = 0; known_channels[i].name != NULL; i++) {
        if (strcasecmp(name, known_channels[i].name) == 0)
            return known_channels[i].dispatcher;
    }
    
    return -1;  /* Unknown channel */
}

/*
 * Initialize channel registry for a device
 */
int sunpci_channel_init(struct sunpci_device *dev)
{
    struct sunpci_channel_registry *reg;
    
    reg = kzalloc(sizeof(*reg), GFP_KERNEL);
    if (!reg)
        return -ENOMEM;
    
    mutex_init(&reg->lock);
    reg->next_id = 1;  /* Channel IDs start at 1 */
    
    dev->channel_registry = reg;
    
    return 0;
}

/*
 * Cleanup channel registry
 */
void sunpci_channel_cleanup(struct sunpci_device *dev)
{
    if (dev->channel_registry) {
        kfree(dev->channel_registry);
        dev->channel_registry = NULL;
    }
}

/*
 * Create a named channel
 *
 * Called when NT driver calls SunPCiIpcCreateChannel()
 */
int sunpci_channel_create(struct sunpci_device *dev,
                          const struct sunpci_channel_create_req *req,
                          struct sunpci_channel_create_rsp *rsp)
{
    struct sunpci_channel_registry *reg = dev->channel_registry;
    struct sunpci_channel *ch;
    char name[SUNPCI_CHANNEL_NAME_MAX + 1];
    int dispatcher;
    int i;
    
    if (!reg) {
        rsp->status = cpu_to_le32(1);  /* Error */
        rsp->channel_id = 0;
        return -EINVAL;
    }
    
    /* Convert channel name to ASCII */
    utf16le_to_ascii(req->name, le32_to_cpu(req->name_len), 
                     name, sizeof(name));
    
    dev_dbg(&dev->pdev->dev, "channel create: '%s'\n", name);
    
    /* Look up dispatcher for this channel name */
    dispatcher = channel_name_to_dispatcher(name);
    if (dispatcher < 0) {
        dev_warn(&dev->pdev->dev, "unknown channel: '%s'\n", name);
        rsp->status = cpu_to_le32(2);  /* Unknown channel */
        rsp->channel_id = 0;
        return 0;
    }
    
    mutex_lock(&reg->lock);
    
    /* Check if channel already exists */
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (reg->channels[i].active && 
            strcmp(reg->channels[i].name, name) == 0) {
            /* Channel exists - check if exclusive */
            if (reg->channels[i].flags & CHANNEL_FLAG_EXCLUSIVE) {
                mutex_unlock(&reg->lock);
                rsp->status = cpu_to_le32(3);  /* Already exists (exclusive) */
                rsp->channel_id = 0;
                return 0;
            }
            /* Return existing channel */
            rsp->status = 0;
            rsp->channel_id = cpu_to_le32(reg->channels[i].id);
            mutex_unlock(&reg->lock);
            return 0;
        }
    }
    
    /* Find free slot */
    ch = NULL;
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (!reg->channels[i].active) {
            ch = &reg->channels[i];
            break;
        }
    }
    
    if (!ch) {
        mutex_unlock(&reg->lock);
        rsp->status = cpu_to_le32(4);  /* No free slots */
        rsp->channel_id = 0;
        return 0;
    }
    
    /* Create the channel */
    ch->id = reg->next_id++;
    ch->dispatcher = dispatcher;
    ch->flags = le32_to_cpu(req->flags);
    ch->active = true;
    strscpy(ch->name, name, sizeof(ch->name));
    
    mutex_unlock(&reg->lock);
    
    dev_info(&dev->pdev->dev, "channel '%s' created (id=%u, disp=%u)\n",
             name, ch->id, ch->dispatcher);
    
    rsp->status = 0;
    rsp->channel_id = cpu_to_le32(ch->id);
    
    return 0;
}

/*
 * Delete a channel
 */
int sunpci_channel_delete(struct sunpci_device *dev, u32 channel_id)
{
    struct sunpci_channel_registry *reg = dev->channel_registry;
    int i;
    
    if (!reg)
        return -EINVAL;
    
    mutex_lock(&reg->lock);
    
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (reg->channels[i].active && reg->channels[i].id == channel_id) {
            dev_info(&dev->pdev->dev, "channel '%s' deleted\n",
                     reg->channels[i].name);
            memset(&reg->channels[i], 0, sizeof(reg->channels[i]));
            mutex_unlock(&reg->lock);
            return 0;
        }
    }
    
    mutex_unlock(&reg->lock);
    return -ENOENT;
}

/*
 * Look up channel by ID
 */
static struct sunpci_channel *channel_lookup(struct sunpci_device *dev, u32 channel_id)
{
    struct sunpci_channel_registry *reg = dev->channel_registry;
    int i;
    
    if (!reg)
        return NULL;
    
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (reg->channels[i].active && reg->channels[i].id == channel_id)
            return &reg->channels[i];
    }
    
    return NULL;
}

/*
 * Get dispatcher ID for a channel
 */
int sunpci_channel_get_dispatcher(struct sunpci_device *dev, u32 channel_id)
{
    struct sunpci_channel *ch;
    
    ch = channel_lookup(dev, channel_id);
    if (!ch)
        return -ENOENT;
    
    return ch->dispatcher;
}

/*
 * Handle NT disk request format
 *
 * NT uses a different packet format than DOS/Win9x.
 * This function translates NT format to the standard storage request format.
 */
int sunpci_channel_handle_nt_disk(struct sunpci_device *dev,
                                  u32 channel_id,
                                  const void *request, size_t req_len,
                                  void *response, size_t *rsp_len)
{
    struct sunpci_channel *ch;
    const struct sunpci_nt_disk_req *nt_req = request;
    struct sunpci_nt_disk_rsp *nt_rsp = response;
    struct sunpci_storage_req storage_req;
    struct sunpci_storage_rsp storage_rsp;
    u8 *data_buf;
    size_t data_len = 64 * 1024;  /* 64KB buffer */
    u32 drive;
    int ret;
    
    if (req_len < sizeof(*nt_req)) {
        return -EINVAL;
    }
    
    ch = channel_lookup(dev, channel_id);
    if (!ch || ch->dispatcher != SUNPCI_DISP_STORAGE) {
        return -EINVAL;
    }
    
    data_buf = kmalloc(data_len, GFP_KERNEL);
    if (!data_buf)
        return -ENOMEM;
    
    /* Map NT drive number to BIOS drive number */
    switch (nt_req->drive_num) {
    case 0: drive = 0x00; break;  /* A: */
    case 1: drive = 0x01; break;  /* B: */
    case 2: drive = 0x80; break;  /* C: */
    case 3: drive = 0x81; break;  /* D: */
    case 4: drive = 0xE0; break;  /* CD-ROM */
    default:
        kfree(data_buf);
        return -EINVAL;
    }
    
    /* Initialize storage request */
    memset(&storage_req, 0, sizeof(storage_req));
    storage_req.drive = cpu_to_le32(drive);
    
    /* Translate NT command to storage command */
    switch (nt_req->command) {
    case NT_DISK_CMD_READ:
        storage_req.command = cpu_to_le32(STORAGE_CMD_READ);
        /* Extract LBA and count from NT-specific format after header */
        if (req_len >= sizeof(*nt_req) + 8) {
            const u8 *extra = (const u8 *)(nt_req + 1);
            u32 lba = extra[0] | (extra[1] << 8) | (extra[2] << 16) | (extra[3] << 24);
            u32 count = extra[4] | (extra[5] << 8);
            storage_req.lba_lo = cpu_to_le32(lba);
            storage_req.count = cpu_to_le32(count);
        }
        break;
        
    case NT_DISK_CMD_WRITE:
        storage_req.command = cpu_to_le32(STORAGE_CMD_WRITE);
        if (req_len >= sizeof(*nt_req) + 8) {
            const u8 *extra = (const u8 *)(nt_req + 1);
            u32 lba = extra[0] | (extra[1] << 8) | (extra[2] << 16) | (extra[3] << 24);
            u32 count = extra[4] | (extra[5] << 8);
            storage_req.lba_lo = cpu_to_le32(lba);
            storage_req.count = cpu_to_le32(count);
            /* Copy write data */
            if (req_len > sizeof(*nt_req) + 8) {
                size_t write_len = req_len - sizeof(*nt_req) - 8;
                if (write_len > data_len)
                    write_len = data_len;
                memcpy(data_buf, extra + 8, write_len);
            }
        }
        break;
        
    case NT_DISK_CMD_GET_PARAMS:
        storage_req.command = cpu_to_le32(STORAGE_CMD_GET_PARAMS);
        break;
        
    case NT_DISK_CMD_SCSI:
        /* SCSI passthrough - use existing SCSI handler */
        if (req_len >= sizeof(*nt_req) + sizeof(struct sunpci_nt_scsi_req)) {
            const struct sunpci_nt_scsi_req *scsi_req = 
                (const struct sunpci_nt_scsi_req *)(nt_req + 1);
            struct sunpci_scsi_req our_scsi;
            struct sunpci_scsi_rsp our_scsi_rsp;
            
            memset(&our_scsi, 0, sizeof(our_scsi));
            memcpy(our_scsi.cdb, scsi_req->cdb, sizeof(our_scsi.cdb));
            our_scsi.cdb_len = cpu_to_le32(scsi_req->cdb_length);
            our_scsi.data_len = scsi_req->xfer_in_len;
            our_scsi.data_direction = cpu_to_le32(
                le32_to_cpu(scsi_req->xfer_out_len) > 0 ? SCSI_DIR_WRITE : 
                le32_to_cpu(scsi_req->xfer_in_len) > 0 ? SCSI_DIR_READ : 
                SCSI_DIR_NONE);
            
            ret = sunpci_storage_scsi_command(dev, &our_scsi, &our_scsi_rsp,
                                              data_buf, data_len);
            
            /* Build NT SCSI response */
            memset(nt_rsp, 0, sizeof(*nt_rsp));
            nt_rsp->command = nt_req->command;
            nt_rsp->response_type = NT_RSP_SCSI;
            
            if (ret == 0 && our_scsi_rsp.status == SCSI_STATUS_GOOD) {
                nt_rsp->error_code = 0;
                nt_rsp->count = le32_to_cpu(our_scsi_rsp.data_len) / 512;
                *rsp_len = sizeof(*nt_rsp) + le32_to_cpu(our_scsi_rsp.data_len);
                if (*rsp_len > sizeof(*nt_rsp))
                    memcpy(nt_rsp + 1, data_buf, le32_to_cpu(our_scsi_rsp.data_len));
            } else {
                nt_rsp->error_code = 0xBB;  /* Undefined error */
                *rsp_len = sizeof(*nt_rsp);
            }
            
            kfree(data_buf);
            return 0;
        }
        break;
        
    default:
        kfree(data_buf);
        dev_dbg(&dev->pdev->dev, "NT: unknown command 0x%02x\n", nt_req->command);
        return -EINVAL;
    }
    
    /* Call storage handler */
    ret = sunpci_storage_handle_request(dev, &storage_req, &storage_rsp,
                                        data_buf, data_len);
    
    /* Build NT response */
    memset(nt_rsp, 0, sizeof(*nt_rsp));
    nt_rsp->command = nt_req->command;
    
    if (ret == 0 && le32_to_cpu(storage_rsp.status) == STORAGE_STATUS_OK) {
        switch (nt_req->command) {
        case NT_DISK_CMD_READ:
            nt_rsp->response_type = NT_RSP_DISK_READ;
            nt_rsp->count = le32_to_cpu(storage_rsp.count);
            *rsp_len = sizeof(*nt_rsp) + (nt_rsp->count * 512);
            memcpy(nt_rsp + 1, data_buf, nt_rsp->count * 512);
            break;
            
        case NT_DISK_CMD_WRITE:
            nt_rsp->response_type = NT_RSP_DISK_READ;  /* Same as read success */
            nt_rsp->count = le32_to_cpu(storage_rsp.count);
            *rsp_len = sizeof(*nt_rsp);
            break;
            
        case NT_DISK_CMD_GET_PARAMS:
            nt_rsp->response_type = NT_RSP_GET_PARAMS;
            *rsp_len = sizeof(*nt_rsp) + sizeof(struct sunpci_storage_params);
            memcpy(nt_rsp + 1, data_buf, sizeof(struct sunpci_storage_params));
            break;
            
        default:
            *rsp_len = sizeof(*nt_rsp);
            break;
        }
        nt_rsp->error_code = 0;
    } else {
        nt_rsp->response_type = NT_RSP_ERROR;
        nt_rsp->error_code = le32_to_cpu(storage_rsp.status);
        *rsp_len = sizeof(*nt_rsp);
    }
    
    kfree(data_buf);
    return 0;
}

/*
 * Handle core channel commands
 */
void sunpci_dispatch_channel(struct sunpci_device *dev,
                             u16 command, u32 sequence,
                             void *payload, size_t payload_len)
{
    struct sunpci_channel_create_req *create_req;
    struct sunpci_channel_create_rsp create_rsp;
    
    switch (command) {
    case CORE_CMD_CHANNEL_CREATE:
        if (payload_len >= sizeof(*create_req)) {
            create_req = payload;
            sunpci_channel_create(dev, create_req, &create_rsp);
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS,
                                     &create_rsp, sizeof(create_rsp));
        } else {
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD,
                                     NULL, 0);
        }
        break;
        
    case CORE_CMD_CHANNEL_DELETE:
        if (payload_len >= sizeof(u32)) {
            u32 channel_id = le32_to_cpup(payload);
            int ret = sunpci_channel_delete(dev, channel_id);
            u32 status = cpu_to_le32(ret == 0 ? 0 : 1);
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS,
                                     &status, sizeof(status));
        } else {
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD,
                                     NULL, 0);
        }
        break;
        
    default:
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD,
                                 NULL, 0);
        break;
    }
}
