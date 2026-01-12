// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Storage passthrough
 *
 * Handles INT 13h BIOS disk service requests from the guest.
 * Provides access to disk images, ISO files, and floppy images.
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uio.h>

#include "sunpci.h"
#include "ipc.h"

/* Sector sizes */
#define SECTOR_SIZE_HD      512
#define SECTOR_SIZE_CDROM   2048
#define SECTOR_SIZE_FLOPPY  512

/* Maximum sectors per transfer */
#define MAX_SECTORS_PER_IO  128

/* SunPCI disk image magic */
#define SUNPCI_DISK_MAGIC       0x53504349  /* "SPCI" at offset 12 */
#define SUNPCI_DISK_MAGIC_OFF   12

/* ISO 9660 signature */
#define ISO9660_MAGIC           "CD001"
#define ISO9660_MAGIC_OFF       (16 * 2048 + 1)  /* Sector 16, offset 1 */
#define ISO9660_MAGIC_LEN       5

/* Valid floppy image sizes */
static const u64 valid_floppy_sizes[] = {
    163840,     /* 160 KB - 5.25" SS/DD */
    184320,     /* 180 KB - 5.25" SS/DD */
    327680,     /* 320 KB - 5.25" DS/DD */
    368640,     /* 360 KB - 5.25" DS/DD */
    737280,     /* 720 KB - 3.5" DD */
    1228800,    /* 1.2 MB - 5.25" HD */
    1474560,    /* 1.44 MB - 3.5" HD */
    2949120,    /* 2.88 MB - 3.5" ED */
};

/* Storage device types for validation */
enum sunpci_storage_type {
    STORAGE_TYPE_HDD,
    STORAGE_TYPE_CDROM,
    STORAGE_TYPE_FLOPPY,
};

/*
 * Storage device context - one per mounted image
 */
struct sunpci_storage_dev {
    struct file *file;          /* Backing file */
    loff_t size;                /* File size in bytes */
    u32 sector_size;            /* Bytes per sector */
    u32 cylinders;              /* CHS: cylinders */
    u32 heads;                  /* CHS: heads */
    u32 sectors;                /* CHS: sectors per track */
    u64 total_sectors;          /* Total sectors */
    bool readonly;              /* Write protected */
    bool mounted;               /* Currently mounted */
};

/*
 * Get storage device for a drive number
 */
static struct sunpci_storage_dev *get_storage_dev(struct sunpci_device *dev,
                                                   u32 drive)
{
    /* Hard disks: 0x80-0x81 -> slots 0-1 */
    if (drive >= 0x80 && drive <= 0x81)
        return dev->storage.disks[drive - 0x80];
    
    /* Floppy: 0x00-0x01 */
    if (drive <= 0x01)
        return dev->storage.floppies[drive];
    
    /* CD-ROM: 0xE0 */
    if (drive == 0xE0)
        return dev->storage.cdrom;
    
    return NULL;
}

/*
 * Calculate CHS geometry for a disk size
 * Uses the same algorithm as the original SunPCI
 */
static void calc_geometry(u64 total_sectors, u32 sector_size,
                         u32 *out_cyls, u32 *out_heads, u32 *out_sects)
{
    u64 size_mb = (total_sectors * sector_size) / (1024 * 1024);
    u32 heads, sectors, cyls;
    
    /* Standard sectors per track */
    sectors = 63;
    
    /* Choose heads based on disk size */
    if (size_mb <= 504)
        heads = 16;
    else if (size_mb <= 1008)
        heads = 32;
    else if (size_mb <= 2016)
        heads = 64;
    else if (size_mb <= 4032)
        heads = 128;
    else
        heads = 255;
    
    /* Calculate cylinders */
    cyls = total_sectors / (heads * sectors);
    if (cyls > 1024)
        cyls = 1024;  /* CHS limit */
    
    *out_cyls = cyls;
    *out_heads = heads;
    *out_sects = sectors;
}

/*
 * Calculate floppy geometry based on image size
 */
static void calc_floppy_geometry(u64 size, u32 *out_cyls, u32 *out_heads,
                                 u32 *out_sects)
{
    /* Standard floppy formats */
    if (size == 1474560) {
        /* 1.44 MB - 3.5" HD */
        *out_cyls = 80;
        *out_heads = 2;
        *out_sects = 18;
    } else if (size == 1228800) {
        /* 1.2 MB - 5.25" HD */
        *out_cyls = 80;
        *out_heads = 2;
        *out_sects = 15;
    } else if (size == 737280) {
        /* 720 KB - 3.5" DD */
        *out_cyls = 80;
        *out_heads = 2;
        *out_sects = 9;
    } else if (size == 368640) {
        /* 360 KB - 5.25" DD */
        *out_cyls = 40;
        *out_heads = 2;
        *out_sects = 9;
    } else if (size == 163840) {
        /* 160 KB - 5.25" SS */
        *out_cyls = 40;
        *out_heads = 1;
        *out_sects = 8;
    } else {
        /* Unknown format - assume 1.44 MB geometry */
        *out_cyls = 80;
        *out_heads = 2;
        *out_sects = 18;
    }
}

/*
 * Validate ISO 9660 image
 * Checks for "CD001" signature at sector 16
 */
static int validate_iso9660(struct file *file, loff_t size)
{
    char buf[8];
    loff_t pos;
    ssize_t ret;
    
    /* ISO must be at least 17 sectors (signature is in sector 16) */
    if (size < 17 * SECTOR_SIZE_CDROM)
        return -EINVAL;
    
    /* Read signature at sector 16, offset 1 */
    pos = ISO9660_MAGIC_OFF;
    ret = kernel_read(file, buf, ISO9660_MAGIC_LEN, &pos);
    if (ret != ISO9660_MAGIC_LEN)
        return -EIO;
    
    if (memcmp(buf, ISO9660_MAGIC, ISO9660_MAGIC_LEN) != 0)
        return -EINVAL;
    
    return 0;
}

/*
 * Validate floppy image by size
 * Must match a known floppy format size
 */
static int validate_floppy(loff_t size)
{
    size_t i;
    
    for (i = 0; i < ARRAY_SIZE(valid_floppy_sizes); i++) {
        if (size == valid_floppy_sizes[i])
            return 0;
    }
    
    return -EINVAL;
}

/*
 * Validate hard disk image
 * Checks for SunPCI magic at offset 12, or accepts raw images with MBR
 */
static int validate_hdd(struct file *file, loff_t size)
{
    u8 buf[16];
    u32 magic;
    loff_t pos;
    ssize_t ret;
    
    /* Minimum size: at least one sector */
    if (size < SECTOR_SIZE_HD)
        return -EINVAL;
    
    /* Read first 16 bytes */
    pos = 0;
    ret = kernel_read(file, buf, sizeof(buf), &pos);
    if (ret != sizeof(buf))
        return -EIO;
    
    /* Check for SunPCI magic at offset 12 */
    memcpy(&magic, buf + SUNPCI_DISK_MAGIC_OFF, sizeof(magic));
    if (le32_to_cpu(magic) == SUNPCI_DISK_MAGIC)
        return 0;  /* Valid SunPCI disk image */
    
    /* Also accept raw disk images with MBR signature */
    pos = 510;
    ret = kernel_read(file, buf, 2, &pos);
    if (ret == 2 && buf[0] == 0x55 && buf[1] == 0xAA)
        return 0;  /* Valid MBR signature */
    
    /* Allow any image file - user knows what they're doing */
    pr_warn("sunpci: disk image has no SunPCI or MBR signature, proceeding anyway\n");
    return 0;
}

/*
 * Open and validate a disk image file
 */
static int storage_open_image(struct sunpci_storage_dev *sdev,
                              const char *path, bool readonly,
                              u32 sector_size, enum sunpci_storage_type type)
{
    struct file *file;
    struct inode *inode;
    loff_t size;
    int flags;
    int ret;
    
    flags = readonly ? O_RDONLY : O_RDWR;
    
    file = filp_open(path, flags | O_LARGEFILE, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);
    
    inode = file_inode(file);
    size = i_size_read(inode);
    
    /* Validate the image based on device type */
    switch (type) {
    case STORAGE_TYPE_CDROM:
        ret = validate_iso9660(file, size);
        if (ret < 0) {
            pr_err("sunpci: invalid ISO 9660 image: %s\n", path);
            filp_close(file, NULL);
            return ret;
        }
        break;
        
    case STORAGE_TYPE_FLOPPY:
        ret = validate_floppy(size);
        if (ret < 0) {
            pr_err("sunpci: invalid floppy image size (%lld bytes): %s\n",
                   size, path);
            filp_close(file, NULL);
            return ret;
        }
        break;
        
    case STORAGE_TYPE_HDD:
        ret = validate_hdd(file, size);
        if (ret < 0) {
            pr_err("sunpci: invalid disk image: %s\n", path);
            filp_close(file, NULL);
            return ret;
        }
        break;
    }
    
    sdev->file = file;
    sdev->size = size;
    sdev->sector_size = sector_size;
    sdev->readonly = readonly;
    sdev->total_sectors = size / sector_size;
    
    /* Calculate geometry */
    if (type == STORAGE_TYPE_FLOPPY) {
        calc_floppy_geometry(size, &sdev->cylinders,
                            &sdev->heads, &sdev->sectors);
    } else {
        calc_geometry(sdev->total_sectors, sector_size,
                     &sdev->cylinders, &sdev->heads, &sdev->sectors);
    }
    
    sdev->mounted = true;
    return 0;
}

/*
 * Close a disk image file
 */
static void storage_close_image(struct sunpci_storage_dev *sdev)
{
    if (sdev->file) {
        filp_close(sdev->file, NULL);
        sdev->file = NULL;
    }
    sdev->mounted = false;
}

/*
 * Read sectors from disk image
 */
static int storage_read_sectors(struct sunpci_storage_dev *sdev,
                                u64 lba, u32 count, void *buffer)
{
    loff_t offset;
    ssize_t ret;
    size_t len;
    
    if (!sdev->file || !sdev->mounted)
        return -ENODEV;
    
    if (lba + count > sdev->total_sectors)
        return -EINVAL;
    
    offset = lba * sdev->sector_size;
    len = count * sdev->sector_size;
    
    ret = kernel_read(sdev->file, buffer, len, &offset);
    if (ret < 0)
        return ret;
    if (ret != len)
        return -EIO;
    
    return 0;
}

/*
 * Write sectors to disk image
 */
static int storage_write_sectors(struct sunpci_storage_dev *sdev,
                                 u64 lba, u32 count, const void *buffer)
{
    loff_t offset;
    ssize_t ret;
    size_t len;
    
    if (!sdev->file || !sdev->mounted)
        return -ENODEV;
    
    if (sdev->readonly)
        return -EROFS;
    
    if (lba + count > sdev->total_sectors)
        return -EINVAL;
    
    offset = lba * sdev->sector_size;
    len = count * sdev->sector_size;
    
    ret = kernel_write(sdev->file, buffer, len, &offset);
    if (ret < 0)
        return ret;
    if (ret != len)
        return -EIO;
    
    return 0;
}

/*
 * Convert CHS to LBA
 */
static u64 chs_to_lba(u32 cylinder, u32 head, u32 sector,
                      u32 num_heads, u32 sectors_per_track)
{
    return ((u64)cylinder * num_heads + head) * sectors_per_track + (sector - 1);
}

/*
 * Handle storage request from guest
 */
int sunpci_storage_handle_request(struct sunpci_device *dev,
                                  const struct sunpci_storage_req *req,
                                  struct sunpci_storage_rsp *rsp,
                                  void *data_buf, size_t data_len)
{
    struct sunpci_storage_dev *sdev;
    u64 lba;
    u32 drive, count;
    int ret;
    
    drive = le32_to_cpu(req->drive);
    count = le32_to_cpu(req->count);
    
    sdev = get_storage_dev(dev, drive);
    if (!sdev || !sdev->mounted) {
        rsp->status = cpu_to_le32(STORAGE_STATUS_NO_MEDIA);
        rsp->count = 0;
        return 0;
    }
    
    /* Calculate LBA from CHS or use provided LBA */
    if (le32_to_cpu(req->lba_hi) != 0 || le32_to_cpu(req->lba_lo) != 0) {
        /* Extended INT 13h with LBA */
        lba = ((u64)le32_to_cpu(req->lba_hi) << 32) | le32_to_cpu(req->lba_lo);
    } else {
        /* Standard CHS */
        lba = chs_to_lba(le32_to_cpu(req->cylinder),
                        le32_to_cpu(req->head),
                        le32_to_cpu(req->sector),
                        sdev->heads, sdev->sectors);
    }
    
    switch (le32_to_cpu(req->command)) {
    case STORAGE_CMD_READ:
        if (count > MAX_SECTORS_PER_IO)
            count = MAX_SECTORS_PER_IO;
        if (data_len < count * sdev->sector_size) {
            rsp->status = cpu_to_le32(STORAGE_STATUS_BAD_CMD);
            rsp->count = 0;
            return 0;
        }
        ret = storage_read_sectors(sdev, lba, count, data_buf);
        if (ret < 0) {
            rsp->status = cpu_to_le32(STORAGE_STATUS_SECTOR_NF);
            rsp->count = 0;
        } else {
            rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
            rsp->count = cpu_to_le32(count);
        }
        break;
        
    case STORAGE_CMD_WRITE:
        if (count > MAX_SECTORS_PER_IO)
            count = MAX_SECTORS_PER_IO;
        ret = storage_write_sectors(sdev, lba, count, data_buf);
        if (ret == -EROFS) {
            rsp->status = cpu_to_le32(STORAGE_STATUS_WRITE_PROT);
            rsp->count = 0;
        } else if (ret < 0) {
            rsp->status = cpu_to_le32(STORAGE_STATUS_SECTOR_NF);
            rsp->count = 0;
        } else {
            rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
            rsp->count = cpu_to_le32(count);
        }
        break;
        
    case STORAGE_CMD_VERIFY:
        /* Just check if sectors are valid */
        if (lba + count <= sdev->total_sectors) {
            rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
            rsp->count = cpu_to_le32(count);
        } else {
            rsp->status = cpu_to_le32(STORAGE_STATUS_SECTOR_NF);
            rsp->count = 0;
        }
        break;
        
    case STORAGE_CMD_RESET:
    case STORAGE_CMD_RECAL:
        /* No-op for image files */
        rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
        rsp->count = 0;
        break;
        
    case STORAGE_CMD_GET_PARAMS:
        /* Return drive parameters */
        {
            struct sunpci_storage_params *params = data_buf;
            params->drive_type = cpu_to_le32(drive >= 0x80 ? 3 : 4); /* HD=3, removable=4 */
            params->cylinders = cpu_to_le32(sdev->cylinders);
            params->heads = cpu_to_le32(sdev->heads);
            params->sectors = cpu_to_le32(sdev->sectors);
            params->total_lo = cpu_to_le32((u32)sdev->total_sectors);
            params->total_hi = cpu_to_le32((u32)(sdev->total_sectors >> 32));
            params->sector_size = cpu_to_le32(sdev->sector_size);
            rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
            rsp->count = cpu_to_le32(sizeof(*params));
        }
        break;
        
    case STORAGE_CMD_GET_TYPE:
        /* Return drive type */
        if (drive >= 0xE0)
            rsp->count = cpu_to_le32(5);  /* CD-ROM */
        else if (drive >= 0x80)
            rsp->count = cpu_to_le32(3);  /* Hard disk */
        else
            rsp->count = cpu_to_le32(4);  /* Floppy */
        rsp->status = cpu_to_le32(STORAGE_STATUS_OK);
        break;
        
    default:
        rsp->status = cpu_to_le32(STORAGE_STATUS_BAD_CMD);
        rsp->count = 0;
        break;
    }
    
    return 0;
}

/*
 * Mount a disk image
 */
int sunpci_storage_mount_disk(struct sunpci_device *dev,
                              u32 slot, const char *path, u32 flags)
{
    struct sunpci_storage_dev *sdev;
    bool readonly;
    int ret;
    
    if (slot > 1)
        return -EINVAL;
    
    /* Close existing image if any */
    if (dev->storage.disks[slot]) {
        storage_close_image(dev->storage.disks[slot]);
        kfree(dev->storage.disks[slot]);
        dev->storage.disks[slot] = NULL;
    }
    
    /* Allocate new storage device */
    sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;
    
    readonly = (flags & SUNPCI_DISK_READONLY) != 0;
    ret = storage_open_image(sdev, path, readonly, SECTOR_SIZE_HD, STORAGE_TYPE_HDD);
    if (ret < 0) {
        kfree(sdev);
        return ret;
    }
    
    dev->storage.disks[slot] = sdev;
    
    /* Notify guest that disk is available */
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive;
            __le32 flags;
        } msg;
        
        msg.drive = cpu_to_le32(0x80 + slot);  /* C: or D: */
        msg.flags = cpu_to_le32(flags);
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_MOUNT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Unmount a disk image
 */
int sunpci_storage_unmount_disk(struct sunpci_device *dev, u32 slot)
{
    if (slot > 1)
        return -EINVAL;
    
    /* Close the image file */
    if (dev->storage.disks[slot]) {
        storage_close_image(dev->storage.disks[slot]);
        kfree(dev->storage.disks[slot]);
        dev->storage.disks[slot] = NULL;
    }
    
    /* Notify guest that disk is removed */
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive;
        } msg;
        
        msg.drive = cpu_to_le32(0x80 + slot);
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_UNMOUNT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Mount a CD-ROM ISO image
 */
int sunpci_storage_mount_cdrom(struct sunpci_device *dev, const char *path)
{
    struct sunpci_storage_dev *sdev;
    int ret;
    
    /* Close existing image if any */
    if (dev->storage.cdrom) {
        storage_close_image(dev->storage.cdrom);
        kfree(dev->storage.cdrom);
        dev->storage.cdrom = NULL;
    }
    
    /* Allocate new storage device */
    sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;
    
    ret = storage_open_image(sdev, path, true, SECTOR_SIZE_CDROM, STORAGE_TYPE_CDROM);
    if (ret < 0) {
        kfree(sdev);
        return ret;
    }
    
    dev->storage.cdrom = sdev;
    
    /* Notify guest that CD is inserted */
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive;
            __le32 flags;
        } msg;
        
        msg.drive = cpu_to_le32(0xE0);  /* CD-ROM drive */
        msg.flags = cpu_to_le32(1);     /* Media inserted */
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_MOUNT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Eject CD-ROM
 */
int sunpci_storage_eject_cdrom(struct sunpci_device *dev)
{
    /* Close the image file */
    if (dev->storage.cdrom) {
        storage_close_image(dev->storage.cdrom);
        kfree(dev->storage.cdrom);
        dev->storage.cdrom = NULL;
    }
    
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive;
        } msg;
        
        msg.drive = cpu_to_le32(0xE0);
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_EJECT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Mount a floppy image
 */
int sunpci_storage_mount_floppy(struct sunpci_device *dev,
                                u32 drive, const char *path)
{
    struct sunpci_storage_dev *sdev;
    int ret;
    
    if (drive > 1)
        return -EINVAL;
    
    /* Close existing image if any */
    if (dev->storage.floppies[drive]) {
        storage_close_image(dev->storage.floppies[drive]);
        kfree(dev->storage.floppies[drive]);
        dev->storage.floppies[drive] = NULL;
    }
    
    /* Allocate new storage device */
    sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;
    
    ret = storage_open_image(sdev, path, false, SECTOR_SIZE_FLOPPY, STORAGE_TYPE_FLOPPY);
    if (ret < 0) {
        kfree(sdev);
        return ret;
    }
    
    dev->storage.floppies[drive] = sdev;
    
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive_num;
            __le32 flags;
        } msg;
        
        msg.drive_num = cpu_to_le32(drive);  /* A: or B: */
        msg.flags = cpu_to_le32(1);
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_MOUNT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Eject floppy
 */
int sunpci_storage_eject_floppy(struct sunpci_device *dev, u32 drive)
{
    if (drive > 1)
        return -EINVAL;
    
    /* Close the image file */
    if (dev->storage.floppies[drive]) {
        storage_close_image(dev->storage.floppies[drive]);
        kfree(dev->storage.floppies[drive]);
        dev->storage.floppies[drive] = NULL;
    }
    
    if (dev->state == SUNPCI_STATE_RUNNING) {
        struct {
            __le32 drive_num;
        } msg;
        
        msg.drive_num = cpu_to_le32(drive);
        
        sunpci_ipc_send_cmd(dev, SUNPCI_DISP_STORAGE, STORAGE_CMD_EJECT,
                            &msg, sizeof(msg), NULL);
    }
    
    return 0;
}

/*
 * Cleanup all storage devices - called on device removal
 */
void sunpci_storage_cleanup(struct sunpci_device *dev)
{
    int i;
    
    /* Close hard disks */
    for (i = 0; i < 2; i++) {
        if (dev->storage.disks[i]) {
            storage_close_image(dev->storage.disks[i]);
            kfree(dev->storage.disks[i]);
            dev->storage.disks[i] = NULL;
        }
    }
    
    /* Close CD-ROM */
    if (dev->storage.cdrom) {
        storage_close_image(dev->storage.cdrom);
        kfree(dev->storage.cdrom);
        dev->storage.cdrom = NULL;
    }
    
    /* Close floppies */
    for (i = 0; i < 2; i++) {
        if (dev->storage.floppies[i]) {
            storage_close_image(dev->storage.floppies[i]);
            kfree(dev->storage.floppies[i]);
            dev->storage.floppies[i] = NULL;
        }
    }
}
