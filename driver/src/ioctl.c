/*
 * SunPCi driver - ioctl handling
 *
 * This file implements the userspace interface via ioctl commands.
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "sunpci.h"

/* ============================================================================
 * Session Management
 * ============================================================================ */

static int ioctl_get_version(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_version ver = {
        .major = SUNPCI_VERSION_MAJOR,
        .minor = SUNPCI_VERSION_MINOR,
        .patch = SUNPCI_VERSION_PATCH,
    };

    if (copy_to_user((void __user *)arg, &ver, sizeof(ver)))
        return -EFAULT;

    return 0;
}

static int ioctl_get_status(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_status status = {0};
    ktime_t now;
    u64 uptime_ns, memory_used;

    mutex_lock(&dev->mutex);
    
    status.state = dev->state;
    
    if (dev->state == SUNPCI_STATE_RUNNING) {
        now = ktime_get();
        uptime_ns = ktime_to_ns(ktime_sub(now, dev->start_time));
    } else {
        uptime_ns = 0;
    }
    
    /* Split 64-bit values into lo/hi for 32-bit compat */
    status.uptime_ns_lo = (u32)uptime_ns;
    status.uptime_ns_hi = (u32)(uptime_ns >> 32);
    
    /* CPU usage would require emulation tracking - report 0 for hardware passthrough */
    status.cpu_usage = 0;
    /* Memory is configured, not dynamically tracked for real hardware */
    memory_used = (u64)dev->config.memory_mb * 1024ULL * 1024ULL;
    status.memory_used_lo = (u32)memory_used;
    status.memory_used_hi = (u32)(memory_used >> 32);
    
    mutex_unlock(&dev->mutex);

    if (copy_to_user((void __user *)arg, &status, sizeof(status)))
        return -EFAULT;

    return 0;
}

static int ioctl_start_session(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_session_config cfg;
    int ret = 0;

    if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
        return -EFAULT;

    /* Validate config */
    if (cfg.memory_mb < 1 || cfg.memory_mb > 256)
        return -EINVAL;

    mutex_lock(&dev->mutex);
    
    if (dev->state != SUNPCI_STATE_STOPPED) {
        ret = -EBUSY;
        goto out;
    }

    /* Store configuration */
    dev->config = cfg;
    
    /* Mount primary disk if specified */
    if (cfg.primary_disk[0]) {
        strscpy(dev->storage.disk_path[0], cfg.primary_disk, SUNPCI_MAX_PATH);
    }
    
    /* Mount secondary disk if specified */
    if (cfg.secondary_disk[0]) {
        strscpy(dev->storage.disk_path[1], cfg.secondary_disk, SUNPCI_MAX_PATH);
    }

    dev->state = SUNPCI_STATE_RUNNING;
    dev->start_time = ktime_get();
    
    pr_info("sunpci%d: session started (memory=%uMB)\n", 
            dev->minor, cfg.memory_mb);

out:
    mutex_unlock(&dev->mutex);
    return ret;
}

static int ioctl_stop_session(struct sunpci_device *dev)
{
    int ret = 0;

    mutex_lock(&dev->mutex);
    
    if (dev->state == SUNPCI_STATE_STOPPED) {
        ret = -EINVAL;
        goto out;
    }

    /* Shutdown subsystems */
    sunpci_net_shutdown(dev);
    sunpci_storage_cleanup(dev);

    dev->state = SUNPCI_STATE_STOPPED;
    pr_info("sunpci%d: session stopped\n", dev->minor);

out:
    mutex_unlock(&dev->mutex);
    return ret;
}

static int ioctl_reset_session(struct sunpci_device *dev)
{
    int ret = 0;

    mutex_lock(&dev->mutex);
    
    if (dev->state != SUNPCI_STATE_RUNNING) {
        ret = -EINVAL;
        goto out;
    }

    /* Reset is a soft reboot - just reset the start time for now */
    dev->start_time = ktime_get();
    pr_info("sunpci%d: session reset (Ctrl+Alt+Del)\n", dev->minor);

out:
    mutex_unlock(&dev->mutex);
    return ret;
}

/* ============================================================================
 * Display
 * ============================================================================ */

static int ioctl_get_display(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_display_info info;

    mutex_lock(&dev->mutex);
    info = dev->display.info;
    mutex_unlock(&dev->mutex);

    if (copy_to_user((void __user *)arg, &info, sizeof(info)))
        return -EFAULT;

    return 0;
}

static int ioctl_set_display(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_display_config cfg;

    if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
        return -EFAULT;

    mutex_lock(&dev->mutex);
    dev->display.config = cfg;
    mutex_unlock(&dev->mutex);

    pr_debug("sunpci%d: display config updated (scale=%u)\n", 
             dev->minor, cfg.scale_mode);

    return 0;
}

static int ioctl_get_framebuffer(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_framebuffer fb;

    mutex_lock(&dev->mutex);
    fb = dev->display.framebuffer;
    mutex_unlock(&dev->mutex);

    if (copy_to_user((void __user *)arg, &fb, sizeof(fb)))
        return -EFAULT;

    return 0;
}

/* ============================================================================
 * Storage
 * ============================================================================ */

static int ioctl_mount_disk(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_disk_mount mount;
    int ret;

    if (copy_from_user(&mount, (void __user *)arg, sizeof(mount)))
        return -EFAULT;

    if (mount.slot > 1)
        return -EINVAL;

    mutex_lock(&dev->mutex);
    strscpy(dev->storage.disk_path[mount.slot], mount.path, SUNPCI_MAX_PATH);
    dev->storage.disk_flags[mount.slot] = mount.flags;
    mutex_unlock(&dev->mutex);

    /* Notify storage subsystem */
    ret = sunpci_storage_mount_disk(dev, mount.slot, mount.path, mount.flags);

    pr_info("sunpci%d: mounted disk %u: %s\n", 
            dev->minor, mount.slot, mount.path);

    return ret;
}

static int ioctl_unmount_disk(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_disk_slot slot;
    int ret;

    if (copy_from_user(&slot, (void __user *)arg, sizeof(slot)))
        return -EFAULT;

    if (slot.slot > 1)
        return -EINVAL;

    /* Notify storage subsystem */
    ret = sunpci_storage_unmount_disk(dev, slot.slot);

    mutex_lock(&dev->mutex);
    dev->storage.disk_path[slot.slot][0] = '\0';
    dev->storage.disk_flags[slot.slot] = 0;
    mutex_unlock(&dev->mutex);

    pr_info("sunpci%d: unmounted disk %u\n", dev->minor, slot.slot);

    return ret;
}

static int ioctl_mount_cdrom(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_path path;
    int ret;

    if (copy_from_user(&path, (void __user *)arg, sizeof(path)))
        return -EFAULT;

    mutex_lock(&dev->mutex);
    strscpy(dev->storage.cdrom_path, path.path, SUNPCI_MAX_PATH);
    mutex_unlock(&dev->mutex);

    /* Notify storage subsystem */
    ret = sunpci_storage_mount_cdrom(dev, path.path);

    pr_info("sunpci%d: mounted CD-ROM: %s\n", dev->minor, path.path);

    return ret;
}

static int ioctl_eject_cdrom(struct sunpci_device *dev)
{
    int ret;

    /* Notify storage subsystem */
    ret = sunpci_storage_eject_cdrom(dev);

    mutex_lock(&dev->mutex);
    dev->storage.cdrom_path[0] = '\0';
    mutex_unlock(&dev->mutex);

    pr_info("sunpci%d: ejected CD-ROM\n", dev->minor);

    return ret;
}

static int ioctl_mount_floppy(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_floppy_mount mount;
    int ret;

    if (copy_from_user(&mount, (void __user *)arg, sizeof(mount)))
        return -EFAULT;

    if (mount.drive > 1)
        return -EINVAL;

    mutex_lock(&dev->mutex);
    strscpy(dev->storage.floppy_path[mount.drive], mount.path, SUNPCI_MAX_PATH);
    mutex_unlock(&dev->mutex);

    /* Notify storage subsystem */
    ret = sunpci_storage_mount_floppy(dev, mount.drive, mount.path);

    pr_info("sunpci%d: mounted floppy %c: %s\n", 
            dev->minor, 'A' + mount.drive, mount.path);

    return ret;
}

static int ioctl_eject_floppy(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_floppy_slot slot;
    int ret;

    if (copy_from_user(&slot, (void __user *)arg, sizeof(slot)))
        return -EFAULT;

    if (slot.drive > 1)
        return -EINVAL;

    /* Notify storage subsystem */
    ret = sunpci_storage_eject_floppy(dev, slot.drive);

    mutex_lock(&dev->mutex);
    dev->storage.floppy_path[slot.drive][0] = '\0';
    mutex_unlock(&dev->mutex);

    pr_info("sunpci%d: ejected floppy %c\n", dev->minor, 'A' + slot.drive);

    return ret;
}

/* ============================================================================
 * Input
 * ============================================================================ */

static int ioctl_keyboard_event(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_key_event event;

    if (copy_from_user(&event, (void __user *)arg, sizeof(event)))
        return -EFAULT;

    pr_debug("sunpci%d: key event scancode=0x%x flags=0x%x\n",
             dev->minor, event.scancode, event.flags);

    return sunpci_inject_key(dev, &event);
}

static int ioctl_mouse_event(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_mouse_event event;

    if (copy_from_user(&event, (void __user *)arg, sizeof(event)))
        return -EFAULT;

    pr_debug("sunpci%d: mouse event dx=%d dy=%d buttons=0x%x\n",
             dev->minor, event.dx, event.dy, event.buttons);

    return sunpci_inject_mouse(dev, &event);
}

/* ============================================================================
 * Clipboard
 * ============================================================================ */

static int ioctl_set_clipboard(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_clipboard *clip;
    int ret = 0;

    clip = kmalloc(sizeof(*clip), GFP_KERNEL);
    if (!clip)
        return -ENOMEM;

    if (copy_from_user(clip, (void __user *)arg, sizeof(*clip))) {
        ret = -EFAULT;
        goto out;
    }

    if (clip->length > SUNPCI_MAX_CLIPBOARD) {
        ret = -EINVAL;
        goto out;
    }

    mutex_lock(&dev->mutex);
    dev->clipboard = *clip;
    mutex_unlock(&dev->mutex);

    /* Send to guest via IPC */
    ret = sunpci_clip_set(dev, clip);
    if (ret < 0 && ret != -ENODEV) {
        /* IPC failed but local copy succeeded - log and continue */
        pr_debug("sunpci%d: clipboard IPC failed: %d\n", dev->minor, ret);
        ret = 0;
    }

    pr_debug("sunpci%d: clipboard set (%u bytes)\n", dev->minor, clip->length);

out:
    kfree(clip);
    return ret;
}

static int ioctl_get_clipboard(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_clipboard *clip;
    int ret = 0;

    clip = kmalloc(sizeof(*clip), GFP_KERNEL);
    if (!clip)
        return -ENOMEM;

    /* Try to get fresh clipboard from guest */
    ret = sunpci_clip_get(dev, clip);
    if (ret < 0) {
        /* IPC failed, fall back to cached clipboard */
        mutex_lock(&dev->mutex);
        *clip = dev->clipboard;
        mutex_unlock(&dev->mutex);
        ret = 0;
    }

    if (copy_to_user((void __user *)arg, clip, sizeof(*clip)))
        ret = -EFAULT;

    kfree(clip);
    return ret;
}

/* ============================================================================
 * Drive Mappings
 * ============================================================================ */

static int ioctl_add_drive_map(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_drive_mapping map;
    int i, slot = -1;

    if (copy_from_user(&map, (void __user *)arg, sizeof(map)))
        return -EFAULT;

    /* Validate drive letter (E-Z) */
    if (map.letter < 'E' || map.letter > 'Z')
        return -EINVAL;

    mutex_lock(&dev->mutex);
    
    /* Check if already mapped, or find empty slot */
    for (i = 0; i < SUNPCI_MAX_DRIVE_MAPS; i++) {
        if (dev->drive_maps[i].letter == map.letter) {
            slot = i;
            break;
        }
        if (slot < 0 && dev->drive_maps[i].letter == 0) {
            slot = i;
        }
    }

    if (slot < 0) {
        mutex_unlock(&dev->mutex);
        return -ENOSPC;
    }

    dev->drive_maps[slot].letter = map.letter;
    dev->drive_maps[slot].flags = map.flags;
    strscpy(dev->drive_maps[slot].path, map.path, SUNPCI_MAX_PATH);
    
    mutex_unlock(&dev->mutex);

    pr_info("sunpci%d: mapped drive %c: -> %s\n", 
            dev->minor, map.letter, map.path);

    return 0;
}

static int ioctl_remove_drive_map(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_drive_letter letter;
    int i;

    if (copy_from_user(&letter, (void __user *)arg, sizeof(letter)))
        return -EFAULT;

    mutex_lock(&dev->mutex);
    
    for (i = 0; i < SUNPCI_MAX_DRIVE_MAPS; i++) {
        if (dev->drive_maps[i].letter == letter.letter) {
            dev->drive_maps[i].letter = 0;
            dev->drive_maps[i].path[0] = '\0';
            mutex_unlock(&dev->mutex);
            
            pr_info("sunpci%d: unmapped drive %c:\n", dev->minor, letter.letter);
            return 0;
        }
    }
    
    mutex_unlock(&dev->mutex);
    return -ENOENT;
}

/* ============================================================================
 * Network
 * ============================================================================ */

static int ioctl_set_network(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_network_config cfg;
    int ret;

    if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
        return -EFAULT;

    mutex_lock(&dev->mutex);
    
    /* Store configuration */
    dev->network = cfg;
    
    /* Initialize network if not already done */
    if (!dev->net_dev) {
        ret = sunpci_net_init(dev);
        if (ret < 0) {
            mutex_unlock(&dev->mutex);
            return ret;
        }
    }
    
    /* Configure the network device */
    ret = sunpci_net_configure(dev, &cfg);
    
    mutex_unlock(&dev->mutex);

    if (ret == 0) {
        pr_info("sunpci%d: network configured (interface=%s, MAC=%pM)\n", 
                dev->minor, cfg.interface, cfg.mac_address);
    }

    return ret;
}

static int ioctl_get_network(struct sunpci_device *dev, unsigned long arg)
{
    struct sunpci_network_status status = {0};

    mutex_lock(&dev->mutex);
    sunpci_net_get_status(dev, &status);
    mutex_unlock(&dev->mutex);

    if (copy_to_user((void __user *)arg, &status, sizeof(status)))
        return -EFAULT;

    return 0;
}

/* ============================================================================
 * Main ioctl Handler
 * ============================================================================ */

long sunpci_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct sunpci_device *dev = file->private_data;

    if (_IOC_TYPE(cmd) != SUNPCI_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    /* Session management */
    case SUNPCI_IOC_GET_VERSION:
        return ioctl_get_version(dev, arg);
    case SUNPCI_IOC_GET_STATUS:
        return ioctl_get_status(dev, arg);
    case SUNPCI_IOC_START_SESSION:
        return ioctl_start_session(dev, arg);
    case SUNPCI_IOC_STOP_SESSION:
        return ioctl_stop_session(dev);
    case SUNPCI_IOC_RESET_SESSION:
        return ioctl_reset_session(dev);

    /* Display */
    case SUNPCI_IOC_GET_DISPLAY:
        return ioctl_get_display(dev, arg);
    case SUNPCI_IOC_SET_DISPLAY:
        return ioctl_set_display(dev, arg);
    case SUNPCI_IOC_GET_FRAMEBUFFER:
        return ioctl_get_framebuffer(dev, arg);

    /* Storage */
    case SUNPCI_IOC_MOUNT_DISK:
        return ioctl_mount_disk(dev, arg);
    case SUNPCI_IOC_UNMOUNT_DISK:
        return ioctl_unmount_disk(dev, arg);
    case SUNPCI_IOC_MOUNT_CDROM:
        return ioctl_mount_cdrom(dev, arg);
    case SUNPCI_IOC_EJECT_CDROM:
        return ioctl_eject_cdrom(dev);
    case SUNPCI_IOC_MOUNT_FLOPPY:
        return ioctl_mount_floppy(dev, arg);
    case SUNPCI_IOC_EJECT_FLOPPY:
        return ioctl_eject_floppy(dev, arg);

    /* Input */
    case SUNPCI_IOC_KEYBOARD_EVENT:
        return ioctl_keyboard_event(dev, arg);
    case SUNPCI_IOC_MOUSE_EVENT:
        return ioctl_mouse_event(dev, arg);

    /* Clipboard */
    case SUNPCI_IOC_SET_CLIPBOARD:
        return ioctl_set_clipboard(dev, arg);
    case SUNPCI_IOC_GET_CLIPBOARD:
        return ioctl_get_clipboard(dev, arg);

    /* Drive mappings */
    case SUNPCI_IOC_ADD_DRIVE_MAP:
        return ioctl_add_drive_map(dev, arg);
    case SUNPCI_IOC_REMOVE_DRIVE_MAP:
        return ioctl_remove_drive_map(dev, arg);

    /* Network */
    case SUNPCI_IOC_SET_NETWORK:
        return ioctl_set_network(dev, arg);
    case SUNPCI_IOC_GET_NETWORK:
        return ioctl_get_network(dev, arg);

    default:
        return -ENOTTY;
    }
}
