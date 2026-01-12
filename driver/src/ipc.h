/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SunPCI IPC Protocol Definitions
 *
 * Message format for communication between host and guest via ring buffers.
 * Based on reverse engineering of sunpcidrv and sunpci.vxd.
 */

#ifndef SUNPCI_IPC_H
#define SUNPCI_IPC_H

#include <linux/types.h>

/*
 * Dispatcher IDs - route messages to subsystem handlers
 */
#define SUNPCI_DISP_CORE    0   /* Core system operations */
#define SUNPCI_DISP_VGA     1   /* VGA/framebuffer operations */
#define SUNPCI_DISP_VIDEO   2   /* Accelerated video (DirectDraw) */
#define SUNPCI_DISP_AUDIO   3   /* Audio I/O */
#define SUNPCI_DISP_NETWORK 4   /* Network bridging */
#define SUNPCI_DISP_FSD     5   /* Filesystem redirection */
#define SUNPCI_DISP_INPUT   6   /* Keyboard/mouse events */
#define SUNPCI_DISP_CLIP    7   /* Clipboard transfer */
#define SUNPCI_DISP_STORAGE 8   /* Storage (INT 13h disk/cdrom/floppy) */
#define SUNPCI_DISP_MAX     9

/*
 * Core dispatcher commands (SUNPCI_DISP_CORE)
 */
#define CORE_CMD_INIT           0x0001
#define CORE_CMD_SHUTDOWN       0x0002
#define CORE_CMD_PING           0x0003
#define CORE_CMD_GET_VERSION    0x0004
#define CORE_CMD_SET_FEATURES   0x0005
#define CORE_CMD_GET_FEATURES   0x0006

/*
 * VGA dispatcher commands (SUNPCI_DISP_VGA)
 */
#define VGA_CMD_SET_MODE        0x0001
#define VGA_CMD_GET_MODE        0x0002
#define VGA_CMD_SET_PALETTE     0x0003
#define VGA_CMD_GET_PALETTE     0x0004
#define VGA_CMD_DIRTY_RECT      0x0005
#define VGA_CMD_CURSOR_POS      0x0006
#define VGA_CMD_CURSOR_SHAPE    0x0007

/*
 * Video dispatcher commands (SUNPCI_DISP_VIDEO)
 */
#define VIDEO_CMD_CREATE_SURF   0x0001
#define VIDEO_CMD_DESTROY_SURF  0x0002
#define VIDEO_CMD_LOCK          0x0003
#define VIDEO_CMD_UNLOCK        0x0004
#define VIDEO_CMD_BLT           0x0005
#define VIDEO_CMD_FLIP          0x0006
#define VIDEO_CMD_SET_COLORKEY  0x0007
#define VIDEO_CMD_SET_CLIPLIST  0x0008

/*
 * Input dispatcher commands (SUNPCI_DISP_INPUT)
 */
#define INPUT_CMD_KEYBOARD      0x0001
#define INPUT_CMD_MOUSE_MOVE    0x0002
#define INPUT_CMD_MOUSE_BUTTON  0x0003
#define INPUT_CMD_MOUSE_WHEEL   0x0004

/*
 * Clipboard dispatcher commands (SUNPCI_DISP_CLIP)
 */
#define CLIP_CMD_SET            0x0001  /* Host -> Guest: set clipboard */
#define CLIP_CMD_GET            0x0002  /* Host -> Guest: request clipboard */
#define CLIP_CMD_NOTIFY         0x0003  /* Guest -> Host: clipboard changed */
#define CLIP_CMD_DATA           0x0004  /* Guest -> Host: clipboard data */

/*
 * Network dispatcher commands (SUNPCI_DISP_NETWORK)
 * These handle NDIS protocol requests from the guest
 */
#define NET_CMD_INIT            0x0001  /* Initialize adapter */
#define NET_CMD_OPEN            0x0002  /* Open adapter */
#define NET_CMD_CLOSE           0x0003  /* Close adapter */
#define NET_CMD_SEND            0x0004  /* Send packet */
#define NET_CMD_RECV            0x0005  /* Receive packet */
#define NET_CMD_DATA_READY      0x0006  /* Data ready notification (triggers IRQ) */
#define NET_CMD_SET_MCAST       0x0007  /* Set multicast filter list */
#define NET_CMD_SET_PROMISC     0x0008  /* Set promiscuous mode */
#define NET_CMD_SET_ALLMULTI    0x0009  /* Set all-multicast mode */
#define NET_CMD_GET_STATS       0x000A  /* Get statistics */
#define NET_CMD_INT_REL         0x000B  /* Interrupt release (guest ACK) */

/*
 * Storage dispatcher commands (SUNPCI_DISP_STORAGE)
 * These handle INT 13h BIOS disk service requests
 */
#define STORAGE_CMD_READ        0x0001  /* Read sectors */
#define STORAGE_CMD_WRITE       0x0002  /* Write sectors */
#define STORAGE_CMD_VERIFY      0x0003  /* Verify sectors */
#define STORAGE_CMD_FORMAT      0x0004  /* Format track */
#define STORAGE_CMD_GET_PARAMS  0x0005  /* Get drive parameters */
#define STORAGE_CMD_GET_TYPE    0x0006  /* Get drive type */
#define STORAGE_CMD_RESET       0x0007  /* Reset drive */
#define STORAGE_CMD_RECAL       0x0008  /* Recalibrate */
#define STORAGE_CMD_SEEK        0x0009  /* Seek to cylinder */
#define STORAGE_CMD_EJECT       0x000A  /* Eject media (CD-ROM/floppy) */
#define STORAGE_CMD_MOUNT       0x000B  /* Mount media notification */
#define STORAGE_CMD_UNMOUNT     0x000C  /* Unmount media notification */

/*
 * Message header - prepended to all IPC messages
 *
 * Total header size: 16 bytes
 */
struct sunpci_msg_header {
    __le32 magic;       /* SUNPCI_MSG_MAGIC */
    __le16 dispatcher;  /* Target dispatcher ID */
    __le16 command;     /* Command within dispatcher */
    __le32 sequence;    /* Sequence number for matching responses */
    __le32 payload_len; /* Length of payload following header */
} __packed;

#define SUNPCI_MSG_MAGIC    0x53504349  /* "SPCI" */
#define SUNPCI_MSG_HDR_SIZE sizeof(struct sunpci_msg_header)

/*
 * Response status codes
 */
#define SUNPCI_RSP_SUCCESS      0x0000
#define SUNPCI_RSP_ERROR        0x0001
#define SUNPCI_RSP_INVALID_CMD  0x0002
#define SUNPCI_RSP_INVALID_DISP 0x0003
#define SUNPCI_RSP_TIMEOUT      0x0004
#define SUNPCI_RSP_BUSY         0x0005

/*
 * Response header - returned in response ring
 */
struct sunpci_rsp_header {
    __le32 magic;       /* SUNPCI_MSG_MAGIC */
    __le16 status;      /* Response status code */
    __le16 reserved;
    __le32 sequence;    /* Matches request sequence */
    __le32 payload_len; /* Length of response payload */
} __packed;

/*
 * Core dispatcher payloads
 */
struct sunpci_core_init {
    __le32 host_version;
    __le32 features_supported;
} __packed;

struct sunpci_core_init_rsp {
    __le32 guest_version;
    __le32 features_enabled;
    __le32 shmem_size;
    __le32 framebuffer_size;
} __packed;

/*
 * VGA mode info
 */
struct sunpci_vga_mode {
    __le16 width;
    __le16 height;
    __le16 bpp;
    __le16 flags;
    __le32 pitch;
    __le32 fb_offset;   /* Offset in framebuffer BAR */
} __packed;

/*
 * VGA dirty rectangle - notify host of framebuffer update
 */
struct sunpci_vga_dirty {
    __le16 x;
    __le16 y;
    __le16 width;
    __le16 height;
} __packed;

/*
 * Input events
 */
struct sunpci_input_keyboard {
    __le16 scancode;
    __le16 flags;       /* KEY_PRESSED, KEY_RELEASED, etc */
} __packed;

#define INPUT_KEY_PRESSED   0x0001
#define INPUT_KEY_RELEASED  0x0002
#define INPUT_KEY_EXTENDED  0x0004

struct sunpci_input_mouse {
    __le32 x;
    __le32 y;
    __le32 buttons;
    __le32 wheel;
} __packed;

#define INPUT_MOUSE_LEFT    0x0001
#define INPUT_MOUSE_RIGHT   0x0002
#define INPUT_MOUSE_MIDDLE  0x0004

/*
 * Video surface descriptor (for DirectDraw emulation)
 */
struct sunpci_video_surface {
    __le32 handle;
    __le32 width;
    __le32 height;
    __le32 bpp;
    __le32 pitch;
    __le32 flags;       /* PRIMARY, OFFSCREEN, OVERLAY, etc */
    __le32 caps;        /* Surface capabilities */
    __le32 pixel_format;
    __le32 fb_offset;   /* Offset in shared memory */
} __packed;

#define SURF_FLAG_PRIMARY   0x0001
#define SURF_FLAG_OFFSCREEN 0x0002
#define SURF_FLAG_OVERLAY   0x0004
#define SURF_FLAG_VISIBLE   0x0008

/*
 * BitBlt operation
 */
struct sunpci_video_blt {
    __le32 src_handle;
    __le32 dst_handle;
    __le16 src_x;
    __le16 src_y;
    __le16 dst_x;
    __le16 dst_y;
    __le16 width;
    __le16 height;
    __le32 rop;         /* Raster operation */
    __le32 flags;
} __packed;

/*
 * Maximum message sizes
 */
#define SUNPCI_MAX_MSG_SIZE     (64 * 1024)
#define SUNPCI_MAX_PAYLOAD      (SUNPCI_MAX_MSG_SIZE - SUNPCI_MSG_HDR_SIZE)

/*
 * Clipboard data - for IPC transfer
 *
 * Format values match Windows clipboard formats:
 *   1 = CF_TEXT (ANSI text)
 *   13 = CF_UNICODETEXT (UTF-16LE)
 */
#define CLIP_FORMAT_TEXT        1   /* ANSI/ASCII text */
#define CLIP_FORMAT_UNICODE     13  /* UTF-16LE text */

struct sunpci_clip_data {
    __le32 format;      /* Clipboard format */
    __le32 length;      /* Data length in bytes */
    /* Variable length data follows */
} __packed;

#define SUNPCI_CLIP_MAX_SIZE    (32 * 1024)  /* Max clipboard transfer */

/*
 * Storage request/response structures
 */

/* Drive types for storage operations */
#define STORAGE_DRIVE_HD        0x80    /* Hard disk (0x80-0x8F) */
#define STORAGE_DRIVE_FLOPPY    0x00    /* Floppy (0x00-0x01) */
#define STORAGE_DRIVE_CDROM     0xE0    /* CD-ROM (0xE0-0xEF) */

/* Storage request header - sent from guest */
struct sunpci_storage_req {
    __le32 drive;       /* Drive number (0x00=A:, 0x80=C:, 0xE0=CD) */
    __le32 command;     /* INT 13h function (AH value) */
    __le32 cylinder;    /* Cylinder number */
    __le32 head;        /* Head number */
    __le32 sector;      /* Sector number (1-based for CHS) */
    __le32 count;       /* Number of sectors */
    __le32 lba_lo;      /* LBA low 32 bits (for extended INT 13h) */
    __le32 lba_hi;      /* LBA high 32 bits */
    /* Sector data follows for write operations */
} __packed;

/* Storage response header - sent to guest */
struct sunpci_storage_rsp {
    __le32 status;      /* INT 13h status (AH return value) */
    __le32 count;       /* Actual sectors transferred */
    /* Sector data follows for read operations */
} __packed;

/* Drive parameters response (INT 13h AH=08h / AH=48h) */
struct sunpci_storage_params {
    __le32 drive_type;  /* Drive type */
    __le32 cylinders;   /* Number of cylinders */
    __le32 heads;       /* Number of heads */
    __le32 sectors;     /* Sectors per track */
    __le32 total_lo;    /* Total sectors (low 32 bits) */
    __le32 total_hi;    /* Total sectors (high 32 bits) */
    __le32 sector_size; /* Bytes per sector (typically 512 or 2048) */
} __packed;

/* INT 13h status codes */
#define STORAGE_STATUS_OK           0x00
#define STORAGE_STATUS_BAD_CMD      0x01
#define STORAGE_STATUS_NOT_FOUND    0x02
#define STORAGE_STATUS_WRITE_PROT   0x03
#define STORAGE_STATUS_SECTOR_NF    0x04
#define STORAGE_STATUS_RESET_FAIL   0x05
#define STORAGE_STATUS_MEDIA_CHANGE 0x06
#define STORAGE_STATUS_DRV_PARAM    0x07
#define STORAGE_STATUS_NO_MEDIA     0x0AA
#define STORAGE_STATUS_UNDEFINED    0xBB

/*
 * Network request/response structures
 */

/* Network status codes */
#define NET_STATUS_OK           0x00
#define NET_STATUS_ERROR        0x01
#define NET_STATUS_BAD_CMD      0x02
#define NET_STATUS_BAD_PACKET   0x03
#define NET_STATUS_NO_DATA      0x04
#define NET_STATUS_NO_DEVICE    0x05
#define NET_STATUS_NO_BUFFER    0x06

/* Network request header - sent from guest */
struct sunpci_net_req {
    __le32 command;     /* NET_CMD_* */
    __le32 param1;      /* Command-specific parameter */
    __le32 param2;      /* Command-specific parameter */
    __le32 length;      /* Data length following header */
    /* Packet data follows for send operations */
} __packed;

/* Network response header - sent to guest */
struct sunpci_net_rsp {
    __le32 status;      /* NET_STATUS_* */
    __le32 length;      /* Data length following header */
    /* Packet data follows for receive operations */
} __packed;

/* Network statistics */
struct sunpci_net_stats {
    __le64 rx_packets;
    __le64 tx_packets;
    __le64 rx_bytes;
    __le64 tx_bytes;
    __le64 rx_dropped;
    __le64 tx_dropped;
} __packed;

/*
 * Timeout values (in jiffies)
 */
#define SUNPCI_CMD_TIMEOUT      (HZ * 5)    /* 5 seconds */
#define SUNPCI_INIT_TIMEOUT     (HZ * 10)   /* 10 seconds */

#endif /* SUNPCI_IPC_H */
