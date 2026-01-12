// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Filesystem Redirection (FSD) subsystem
 *
 * Provides transparent access to host filesystem from the guest OS.
 * Windows/DOS sees mapped directories as network drives.
 *
 * Protocol flow:
 *   Guest sunfsd.vxd/sys → IPC message → This handler → VFS → Host files
 *
 * Supported guest drivers:
 *   - DOS: redir.sys + sunpcnet.exe
 *   - Win95/98: sunfsd.vxd  
 *   - WinNT/2000: sunfsd.sys
 *
 * File handles are managed per-device and mapped to kernel file pointers.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/uaccess.h>
#include <linux/time64.h>
#include <linux/hashtable.h>
#include <linux/ctype.h>
#include <linux/fs_struct.h>

#include "sunpci.h"
#include "regs.h"
#include "ipc.h"

/*
 * FSD IPC commands (SUNPCI_DISP_FSD)
 */
#define FSD_CMD_MOUNT           0x0001  /* Add drive mapping */
#define FSD_CMD_UNMOUNT         0x0002  /* Remove drive mapping */
#define FSD_CMD_OPEN            0x0003  /* Open file */
#define FSD_CMD_CLOSE           0x0004  /* Close file */
#define FSD_CMD_READ            0x0005  /* Read data */
#define FSD_CMD_WRITE           0x0006  /* Write data */
#define FSD_CMD_SEEK            0x0007  /* Seek in file */
#define FSD_CMD_STAT            0x0008  /* Get file info */
#define FSD_CMD_MKDIR           0x0009  /* Create directory */
#define FSD_CMD_RMDIR           0x000A  /* Remove directory */
#define FSD_CMD_DELETE          0x000B  /* Delete file */
#define FSD_CMD_RENAME          0x000C  /* Rename file */
#define FSD_CMD_OPENDIR         0x000D  /* Open directory for enumeration */
#define FSD_CMD_READDIR         0x000E  /* Read directory entry */
#define FSD_CMD_CLOSEDIR        0x000F  /* Close directory */
#define FSD_CMD_SETATTR         0x0010  /* Set file attributes */
#define FSD_CMD_STATFS          0x0011  /* Get filesystem stats */
#define FSD_CMD_TRUNCATE        0x0012  /* Truncate file */
#define FSD_CMD_LOCK            0x0013  /* Lock file region */
#define FSD_CMD_UNLOCK          0x0014  /* Unlock file region */

/* Maximum values */
#define FSD_MAX_HANDLES         256     /* Max open file handles */
#define FSD_MAX_PATH            260     /* Max path length (DOS/Win limit) */
#define FSD_MAX_FILENAME        256     /* Max filename component */
#define FSD_MAX_READDIR         64      /* Max entries per READDIR */

/* DOS file attributes */
#define DOS_ATTR_READONLY       0x01
#define DOS_ATTR_HIDDEN         0x02
#define DOS_ATTR_SYSTEM         0x04
#define DOS_ATTR_VOLUME         0x08
#define DOS_ATTR_DIRECTORY      0x10
#define DOS_ATTR_ARCHIVE        0x20

/* Open mode flags */
#define FSD_OPEN_READ           0x0001
#define FSD_OPEN_WRITE          0x0002
#define FSD_OPEN_CREATE         0x0010
#define FSD_OPEN_TRUNCATE       0x0020
#define FSD_OPEN_APPEND         0x0040

/* Seek origins */
#define FSD_SEEK_SET            0
#define FSD_SEEK_CUR            1
#define FSD_SEEK_END            2

/*
 * Open file handle entry
 */
struct fsd_handle {
    struct hlist_node node;
    u32 guest_handle;           /* Handle number guest uses */
    struct file *filp;          /* Kernel file pointer */
    u8 drive_letter;            /* Which mapped drive */
    bool is_directory;          /* Directory vs file */
    char path[FSD_MAX_PATH];    /* Full host path */
};

/*
 * Directory enumeration context
 */
struct fsd_dir_ctx {
    struct hlist_node node;
    u32 guest_handle;
    struct file *filp;
    struct dir_context ctx;
    /* Buffer for readdir results */
    char *entries;
    size_t entries_len;
    size_t entries_pos;
};

/*
 * Per-device FSD state
 */
struct sunpci_fsd_state {
    struct sunpci_device *dev;
    
    /* Handle management */
    DECLARE_HASHTABLE(handles, 8);      /* 256 buckets */
    DECLARE_HASHTABLE(dir_handles, 6);  /* 64 buckets */
    u32 next_handle;
    spinlock_t handle_lock;
    
    /* Statistics */
    u64 files_opened;
    u64 files_closed;
    u64 bytes_read;
    u64 bytes_written;
    u64 dirs_listed;
};

/*
 * DOS time format conversion
 * DOS date: Bits 0-4=Day, 5-8=Month, 9-15=Year-1980
 * DOS time: Bits 0-4=Seconds/2, 5-10=Minutes, 11-15=Hours
 */
static void unix_to_dos_time(time64_t unix_time, u16 *dos_date, u16 *dos_time)
{
    struct tm tm;
    
    time64_to_tm(unix_time, 0, &tm);
    
    *dos_date = ((tm.tm_year - 80) << 9) |
                ((tm.tm_mon + 1) << 5) |
                tm.tm_mday;
    
    *dos_time = (tm.tm_hour << 11) |
                (tm.tm_min << 5) |
                (tm.tm_sec / 2);
}

static __maybe_unused time64_t dos_to_unix_time(u16 dos_date, u16 dos_time)
{
    struct tm tm = {0};
    
    tm.tm_year = ((dos_date >> 9) & 0x7F) + 80;
    tm.tm_mon = ((dos_date >> 5) & 0x0F) - 1;
    tm.tm_mday = dos_date & 0x1F;
    
    tm.tm_hour = (dos_time >> 11) & 0x1F;
    tm.tm_min = (dos_time >> 5) & 0x3F;
    tm.tm_sec = (dos_time & 0x1F) * 2;
    
    return mktime64(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/*
 * Convert Unix mode to DOS attributes
 */
static u8 mode_to_dos_attr(umode_t mode)
{
    u8 attr = 0;
    
    if (S_ISDIR(mode))
        attr |= DOS_ATTR_DIRECTORY;
    if (!(mode & S_IWUSR))
        attr |= DOS_ATTR_READONLY;
    
    /* Archive bit always set for files */
    if (!S_ISDIR(mode))
        attr |= DOS_ATTR_ARCHIVE;
    
    return attr;
}

/*
 * Translate guest path to host path
 * Input: "F:\subdir\file.txt" with F: mapped to /home/user
 * Output: "/home/user/subdir/file.txt"
 */
static int fsd_translate_path(struct sunpci_device *dev,
                              const char *guest_path,
                              char *host_path, size_t host_len)
{
    char drive_letter;
    const char *rel_path;
    int i;
    
    if (!guest_path || strlen(guest_path) < 2)
        return -EINVAL;
    
    /* Extract drive letter */
    drive_letter = toupper(guest_path[0]);
    if (guest_path[1] != ':')
        return -EINVAL;
    
    /* Skip drive and colon, handle backslash */
    rel_path = guest_path + 2;
    if (*rel_path == '\\' || *rel_path == '/')
        rel_path++;
    
    /* Find mapping for this drive */
    for (i = 0; i < SUNPCI_MAX_DRIVE_MAPS; i++) {
        if (dev->drive_maps[i].letter == drive_letter) {
            /* Found mapping - concatenate paths */
            size_t base_len = strlen(dev->drive_maps[i].path);
            size_t rel_len = strlen(rel_path);
            char *p;
            
            if (base_len + rel_len + 2 > host_len)
                return -ENAMETOOLONG;
            
            /* Build host path */
            strcpy(host_path, dev->drive_maps[i].path);
            
            /* Add separator if needed */
            if (base_len > 0 && host_path[base_len-1] != '/')
                strcat(host_path, "/");
            
            strcat(host_path, rel_path);
            
            /* Convert backslashes to forward slashes */
            for (p = host_path; *p; p++) {
                if (*p == '\\')
                    *p = '/';
            }
            
            return 0;
        }
    }
    
    return -ENOENT;  /* No mapping for this drive */
}

/*
 * Allocate a new file handle
 */
static struct fsd_handle *fsd_alloc_handle(struct sunpci_fsd_state *fsd)
{
    struct fsd_handle *h;
    unsigned long flags;
    
    h = kzalloc(sizeof(*h), GFP_KERNEL);
    if (!h)
        return NULL;
    
    spin_lock_irqsave(&fsd->handle_lock, flags);
    h->guest_handle = ++fsd->next_handle;
    if (h->guest_handle == 0)
        h->guest_handle = ++fsd->next_handle;  /* Skip 0 */
    hash_add(fsd->handles, &h->node, h->guest_handle);
    spin_unlock_irqrestore(&fsd->handle_lock, flags);
    
    return h;
}

/*
 * Find handle by guest handle number
 */
static struct fsd_handle *fsd_find_handle(struct sunpci_fsd_state *fsd, u32 handle)
{
    struct fsd_handle *h;
    unsigned long flags;
    
    spin_lock_irqsave(&fsd->handle_lock, flags);
    hash_for_each_possible(fsd->handles, h, node, handle) {
        if (h->guest_handle == handle) {
            spin_unlock_irqrestore(&fsd->handle_lock, flags);
            return h;
        }
    }
    spin_unlock_irqrestore(&fsd->handle_lock, flags);
    
    return NULL;
}

/*
 * Free a file handle
 */
static void fsd_free_handle(struct sunpci_fsd_state *fsd, struct fsd_handle *h)
{
    unsigned long flags;
    
    spin_lock_irqsave(&fsd->handle_lock, flags);
    hash_del(&h->node);
    spin_unlock_irqrestore(&fsd->handle_lock, flags);
    
    if (h->filp)
        filp_close(h->filp, NULL);
    
    kfree(h);
}

/*
 * Initialize FSD subsystem
 */
int sunpci_fsd_init(struct sunpci_device *dev)
{
    struct sunpci_fsd_state *fsd;
    
    fsd = kzalloc(sizeof(*fsd), GFP_KERNEL);
    if (!fsd)
        return -ENOMEM;
    
    fsd->dev = dev;
    hash_init(fsd->handles);
    hash_init(fsd->dir_handles);
    spin_lock_init(&fsd->handle_lock);
    fsd->next_handle = 0;
    
    dev->fsd_state = fsd;
    
    pr_info("sunpci%d: filesystem redirection initialized\n", dev->minor);
    return 0;
}

/*
 * Shutdown FSD subsystem
 */
void sunpci_fsd_shutdown(struct sunpci_device *dev)
{
    struct sunpci_fsd_state *fsd = dev->fsd_state;
    struct fsd_handle *h;
    struct hlist_node *tmp;
    int bkt;
    
    if (!fsd)
        return;
    
    /* Close all open handles */
    hash_for_each_safe(fsd->handles, bkt, tmp, h, node) {
        fsd_free_handle(fsd, h);
    }
    
    kfree(fsd);
    dev->fsd_state = NULL;
}

/*
 * Handle FSD_CMD_OPEN
 */
static int fsd_handle_open(struct sunpci_fsd_state *fsd,
                           const void *payload, size_t len,
                           void *response, size_t *rsp_len)
{
    struct {
        __le32 flags;
        __le16 path_len;
        char path[FSD_MAX_PATH];
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
        __le32 handle;
    } __packed *rsp = response;
    
    struct fsd_handle *h;
    char host_path[512];
    int open_flags;
    int ret;
    
    if (len < 6)
        return -EINVAL;
    
    /* Translate path */
    ret = fsd_translate_path(fsd->dev, req->path, host_path, sizeof(host_path));
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        rsp->handle = 0;
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    /* Convert flags */
    open_flags = O_LARGEFILE;
    if ((le32_to_cpu(req->flags) & (FSD_OPEN_READ | FSD_OPEN_WRITE)) ==
        (FSD_OPEN_READ | FSD_OPEN_WRITE))
        open_flags |= O_RDWR;
    else if (le32_to_cpu(req->flags) & FSD_OPEN_WRITE)
        open_flags |= O_WRONLY;
    else
        open_flags |= O_RDONLY;
    
    if (le32_to_cpu(req->flags) & FSD_OPEN_CREATE)
        open_flags |= O_CREAT;
    if (le32_to_cpu(req->flags) & FSD_OPEN_TRUNCATE)
        open_flags |= O_TRUNC;
    if (le32_to_cpu(req->flags) & FSD_OPEN_APPEND)
        open_flags |= O_APPEND;
    
    /* Allocate handle */
    h = fsd_alloc_handle(fsd);
    if (!h) {
        rsp->status = cpu_to_le32(ENOMEM);
        rsp->handle = 0;
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    /* Open file */
    h->filp = filp_open(host_path, open_flags, 0644);
    if (IS_ERR(h->filp)) {
        ret = PTR_ERR(h->filp);
        h->filp = NULL;
        fsd_free_handle(fsd, h);
        rsp->status = cpu_to_le32(-ret);
        rsp->handle = 0;
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    strscpy(h->path, host_path, sizeof(h->path));
    h->is_directory = S_ISDIR(file_inode(h->filp)->i_mode);
    
    fsd->files_opened++;
    
    rsp->status = 0;
    rsp->handle = cpu_to_le32(h->guest_handle);
    *rsp_len = sizeof(*rsp);
    
    pr_debug("sunpci%d: fsd open %s -> handle %u\n",
             fsd->dev->minor, host_path, h->guest_handle);
    
    return 0;
}

/*
 * Handle FSD_CMD_CLOSE
 */
static int fsd_handle_close(struct sunpci_fsd_state *fsd,
                            const void *payload, size_t len,
                            void *response, size_t *rsp_len)
{
    struct {
        __le32 handle;
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
    } __packed *rsp = response;
    
    struct fsd_handle *h;
    u32 handle;
    
    if (len < 4)
        return -EINVAL;
    
    handle = le32_to_cpu(req->handle);
    h = fsd_find_handle(fsd, handle);
    if (!h) {
        rsp->status = cpu_to_le32(EBADF);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    pr_debug("sunpci%d: fsd close handle %u\n", fsd->dev->minor, handle);
    
    fsd_free_handle(fsd, h);
    fsd->files_closed++;
    
    rsp->status = 0;
    *rsp_len = sizeof(*rsp);
    return 0;
}

/*
 * Handle FSD_CMD_READ
 */
static int fsd_handle_read(struct sunpci_fsd_state *fsd,
                           const void *payload, size_t len,
                           void *response, size_t *rsp_len)
{
    struct {
        __le32 handle;
        __le32 count;
        __le64 offset;
    } __packed *req = (void *)payload;
    
    /* Response: status + data */
    struct {
        __le32 status;
        __le32 bytes_read;
        u8 data[];
    } __packed *rsp = response;
    
    struct fsd_handle *h;
    loff_t pos;
    ssize_t bytes;
    u32 count;
    
    if (len < 16)
        return -EINVAL;
    
    h = fsd_find_handle(fsd, le32_to_cpu(req->handle));
    if (!h) {
        rsp->status = cpu_to_le32(EBADF);
        rsp->bytes_read = 0;
        *rsp_len = 8;
        return 0;
    }
    
    count = le32_to_cpu(req->count);
    if (count > 32768)  /* Limit read size */
        count = 32768;
    
    pos = le64_to_cpu(req->offset);
    bytes = kernel_read(h->filp, rsp->data, count, &pos);
    
    if (bytes < 0) {
        rsp->status = cpu_to_le32(-bytes);
        rsp->bytes_read = 0;
        *rsp_len = 8;
    } else {
        rsp->status = 0;
        rsp->bytes_read = cpu_to_le32(bytes);
        *rsp_len = 8 + bytes;
        fsd->bytes_read += bytes;
    }
    
    return 0;
}

/*
 * Handle FSD_CMD_WRITE
 */
static int fsd_handle_write(struct sunpci_fsd_state *fsd,
                            const void *payload, size_t len,
                            void *response, size_t *rsp_len)
{
    struct {
        __le32 handle;
        __le32 count;
        __le64 offset;
        u8 data[];
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
        __le32 bytes_written;
    } __packed *rsp = response;
    
    struct fsd_handle *h;
    loff_t pos;
    ssize_t bytes;
    u32 count;
    
    if (len < 16)
        return -EINVAL;
    
    h = fsd_find_handle(fsd, le32_to_cpu(req->handle));
    if (!h) {
        rsp->status = cpu_to_le32(EBADF);
        rsp->bytes_written = 0;
        *rsp_len = 8;
        return 0;
    }
    
    count = le32_to_cpu(req->count);
    if (count > len - 16)
        count = len - 16;
    
    pos = le64_to_cpu(req->offset);
    bytes = kernel_write(h->filp, req->data, count, &pos);
    
    if (bytes < 0) {
        rsp->status = cpu_to_le32(-bytes);
        rsp->bytes_written = 0;
    } else {
        rsp->status = 0;
        rsp->bytes_written = cpu_to_le32(bytes);
        fsd->bytes_written += bytes;
    }
    
    *rsp_len = 8;
    return 0;
}

/*
 * Handle FSD_CMD_STAT
 */
static int fsd_handle_stat(struct sunpci_fsd_state *fsd,
                           const void *payload, size_t len,
                           void *response, size_t *rsp_len)
{
    struct {
        __le16 path_len;
        char path[FSD_MAX_PATH];
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
        __le32 size_low;
        __le32 size_high;
        __le16 date;
        __le16 time;
        u8 attr;
        u8 reserved[3];
    } __packed *rsp = response;
    
    char host_path[512];
    struct path path;
    struct kstat stat;
    int ret;
    
    if (len < 2)
        return -EINVAL;
    
    ret = fsd_translate_path(fsd->dev, req->path, host_path, sizeof(host_path));
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    ret = kern_path(host_path, LOOKUP_FOLLOW, &path);
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    ret = vfs_getattr(&path, &stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
    path_put(&path);
    
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    rsp->status = 0;
    rsp->size_low = cpu_to_le32(stat.size & 0xFFFFFFFF);
    rsp->size_high = cpu_to_le32(stat.size >> 32);
    unix_to_dos_time(stat.mtime.tv_sec, &rsp->date, &rsp->time);
    rsp->attr = mode_to_dos_attr(stat.mode);
    memset(rsp->reserved, 0, sizeof(rsp->reserved));
    
    *rsp_len = sizeof(*rsp);
    return 0;
}

/*
 * Handle FSD_CMD_MKDIR
 * Note: For kernel-space directory creation we need proper VFS locking.
 * In practice, the daemon would handle this via userspace syscalls.
 * This is a stub that will work for basic testing.
 */
static int fsd_handle_mkdir(struct sunpci_fsd_state *fsd,
                            const void *payload, size_t len,
                            void *response, size_t *rsp_len)
{
    struct {
        __le16 path_len;
        char path[FSD_MAX_PATH];
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
    } __packed *rsp = response;
    
    char host_path[512];
    int ret;
    
    if (len < 2)
        return -EINVAL;
    
    ret = fsd_translate_path(fsd->dev, req->path, host_path, sizeof(host_path));
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    /* 
     * Directory creation from kernel space is complex due to VFS locking.
     * For now, indicate this should be handled by userspace daemon.
     * Return ENOSYS to indicate unimplemented.
     */
    pr_debug("sunpci%d: mkdir request for %s (delegated to userspace)\n",
             fsd->dev->minor, host_path);
    
    rsp->status = cpu_to_le32(ENOSYS);
    *rsp_len = sizeof(*rsp);
    return 0;
}

/*
 * Handle FSD_CMD_DELETE
 * Note: For kernel-space file deletion we need proper VFS locking.
 * In practice, the daemon would handle this via userspace syscalls.
 */
static int fsd_handle_delete(struct sunpci_fsd_state *fsd,
                             const void *payload, size_t len,
                             void *response, size_t *rsp_len)
{
    struct {
        __le16 path_len;
        char path[FSD_MAX_PATH];
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
    } __packed *rsp = response;
    
    char host_path[512];
    int ret;
    
    if (len < 2)
        return -EINVAL;
    
    ret = fsd_translate_path(fsd->dev, req->path, host_path, sizeof(host_path));
    if (ret) {
        rsp->status = cpu_to_le32(-ret);
        *rsp_len = sizeof(*rsp);
        return 0;
    }
    
    /* 
     * File deletion from kernel space requires complex VFS locking.
     * Delegate to userspace daemon for proper handling.
     */
    pr_debug("sunpci%d: delete request for %s (delegated to userspace)\n",
             fsd->dev->minor, host_path);
    
    rsp->status = cpu_to_le32(ENOSYS);
    *rsp_len = sizeof(*rsp);
    return 0;
}

/*
 * Handle FSD_CMD_STATFS
 */
static int fsd_handle_statfs(struct sunpci_fsd_state *fsd,
                             const void *payload, size_t len,
                             void *response, size_t *rsp_len)
{
    struct {
        u8 drive_letter;
    } __packed *req = (void *)payload;
    
    struct {
        __le32 status;
        __le32 total_clusters;
        __le32 free_clusters;
        __le32 sectors_per_cluster;
        __le32 bytes_per_sector;
    } __packed *rsp = response;
    
    struct path path;
    struct kstatfs statfs;
    int i, ret;
    char drive = toupper(req->drive_letter);
    
    if (len < 1)
        return -EINVAL;
    
    /* Find mapping and get statfs */
    for (i = 0; i < SUNPCI_MAX_DRIVE_MAPS; i++) {
        if (fsd->dev->drive_maps[i].letter == drive) {
            ret = kern_path(fsd->dev->drive_maps[i].path, LOOKUP_FOLLOW, &path);
            if (ret) {
                rsp->status = cpu_to_le32(-ret);
                *rsp_len = sizeof(*rsp);
                return 0;
            }
            
            ret = vfs_statfs(&path, &statfs);
            path_put(&path);
            
            if (ret) {
                rsp->status = cpu_to_le32(-ret);
                *rsp_len = sizeof(*rsp);
                return 0;
            }
            
            /* Convert to DOS-friendly format */
            rsp->status = 0;
            rsp->total_clusters = cpu_to_le32(statfs.f_blocks);
            rsp->free_clusters = cpu_to_le32(statfs.f_bfree);
            rsp->sectors_per_cluster = cpu_to_le32(statfs.f_bsize / 512);
            rsp->bytes_per_sector = cpu_to_le32(512);
            *rsp_len = sizeof(*rsp);
            return 0;
        }
    }
    
    rsp->status = cpu_to_le32(ENOENT);
    *rsp_len = sizeof(*rsp);
    return 0;
}

/*
 * Handle FSD IPC message
 */
int sunpci_fsd_handle_message(struct sunpci_device *dev,
                              u16 command,
                              const void *payload, size_t len,
                              void *response, size_t *rsp_len)
{
    struct sunpci_fsd_state *fsd = dev->fsd_state;
    
    if (!fsd)
        return -ENODEV;
    
    switch (command) {
    case FSD_CMD_OPEN:
        return fsd_handle_open(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_CLOSE:
        return fsd_handle_close(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_READ:
        return fsd_handle_read(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_WRITE:
        return fsd_handle_write(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_STAT:
        return fsd_handle_stat(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_MKDIR:
        return fsd_handle_mkdir(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_DELETE:
        return fsd_handle_delete(fsd, payload, len, response, rsp_len);
        
    case FSD_CMD_STATFS:
        return fsd_handle_statfs(fsd, payload, len, response, rsp_len);
        
    /* TODO: Implement remaining commands */
    case FSD_CMD_SEEK:
    case FSD_CMD_RMDIR:
    case FSD_CMD_RENAME:
    case FSD_CMD_OPENDIR:
    case FSD_CMD_READDIR:
    case FSD_CMD_CLOSEDIR:
    case FSD_CMD_SETATTR:
    case FSD_CMD_TRUNCATE:
    case FSD_CMD_LOCK:
    case FSD_CMD_UNLOCK:
        pr_debug("sunpci%d: FSD command %04x not yet implemented\n",
                 dev->minor, command);
        return -ENOSYS;
        
    default:
        pr_debug("sunpci%d: unknown FSD command %04x\n",
                 dev->minor, command);
        return -EINVAL;
    }
}

/*
 * Get FSD statistics
 */
void sunpci_fsd_get_stats(struct sunpci_device *dev,
                          u64 *opened, u64 *closed,
                          u64 *read, u64 *written)
{
    struct sunpci_fsd_state *fsd = dev->fsd_state;
    
    if (!fsd) {
        if (opened) *opened = 0;
        if (closed) *closed = 0;
        if (read) *read = 0;
        if (written) *written = 0;
        return;
    }
    
    if (opened) *opened = fsd->files_opened;
    if (closed) *closed = fsd->files_closed;
    if (read) *read = fsd->bytes_read;
    if (written) *written = fsd->bytes_written;
}
