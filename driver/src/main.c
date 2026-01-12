// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - main module
 *
 * This driver provides an interface for the SunPCI card.
 * The driver will only load if actual SunPCI hardware is detected.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "sunpci.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rising Sun Project");
MODULE_DESCRIPTION("SunPCI driver");
MODULE_VERSION("0.1.0");

/* Global variables */
struct class *sunpci_class;
int sunpci_major;
static dev_t sunpci_devt;
static struct sunpci_device *sunpci_devices[SUNPCI_MAX_DEVICES];

/* File operations */
static int sunpci_open(struct inode *inode, struct file *file)
{
    struct sunpci_device *dev;
    int minor = iminor(inode);

    if (minor >= SUNPCI_MAX_DEVICES)
        return -ENODEV;

    dev = sunpci_devices[minor];
    if (!dev)
        return -ENODEV;

    file->private_data = dev;

    pr_debug("sunpci: device %d opened\n", minor);
    return 0;
}

static int sunpci_release(struct inode *inode, struct file *file)
{
    struct sunpci_device *dev = file->private_data;

    pr_debug("sunpci: device %d closed\n", dev->minor);
    return 0;
}

const struct file_operations sunpci_fops = {
    .owner = THIS_MODULE,
    .open = sunpci_open,
    .release = sunpci_release,
    .unlocked_ioctl = sunpci_ioctl,
    .compat_ioctl = sunpci_ioctl,
    .mmap = sunpci_mmap,
};

/* Initialize default display state */
static void sunpci_init_display(struct sunpci_device *dev)
{
    /* Default to VGA text mode */
    dev->display.info.width = 720;
    dev->display.info.height = 400;
    dev->display.info.color_depth = 4;
    dev->display.info.mode = SUNPCI_DISPLAY_MODE_TEXT;
    dev->display.info.text_cols = 80;
    dev->display.info.text_rows = 25;
    
    /* Default display config */
    dev->display.config.scale_mode = SUNPCI_SCALE_FIT;
    dev->display.config.scale_factor = 1;
    dev->display.config.flags = SUNPCI_DISPLAY_MAINTAIN_ASPECT;
}

/**
 * sunpci_create_device - Create a device instance
 * @minor: Minor device number
 * @pdev: PCI device (or NULL for testing)
 *
 * Called from PCI probe when hardware is detected.
 */
struct sunpci_device *sunpci_create_device(int minor, struct pci_dev *pdev)
{
    struct sunpci_device *dev;
    struct device *parent = pdev ? &pdev->dev : NULL;
    int ret;

    if (minor >= SUNPCI_MAX_DEVICES)
        return ERR_PTR(-EINVAL);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return ERR_PTR(-ENOMEM);

    dev->minor = minor;
    dev->pdev = pdev;
    mutex_init(&dev->mutex);
    dev->state = SUNPCI_STATE_STOPPED;
    
    /* Default configuration */
    dev->config.memory_mb = 64;
    dev->config.flags = SUNPCI_FLAG_NETWORK_ENABLED | 
                        SUNPCI_FLAG_CLIPBOARD_ENABLED |
                        SUNPCI_FLAG_CLIPBOARD_TO_HOST |
                        SUNPCI_FLAG_CLIPBOARD_TO_GUEST;

    sunpci_init_display(dev);

    /* Initialize character device */
    cdev_init(&dev->cdev, &sunpci_fops);
    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev, MKDEV(sunpci_major, minor), 1);
    if (ret) {
        kfree(dev);
        return ERR_PTR(ret);
    }

    /* Create device node with PCI device as parent */
    dev->dev = device_create(sunpci_class, parent, MKDEV(sunpci_major, minor),
                             dev, "sunpci%d", minor);
    if (IS_ERR(dev->dev)) {
        cdev_del(&dev->cdev);
        kfree(dev);
        return ERR_CAST(dev->dev);
    }

    sunpci_devices[minor] = dev;
    pr_info("sunpci: created device sunpci%d\n", minor);

    return dev;
}

/**
 * sunpci_destroy_device - Destroy a device instance
 * @dev: Device to destroy
 *
 * Called from PCI remove when hardware is removed.
 */
void sunpci_destroy_device(struct sunpci_device *dev)
{
    if (!dev)
        return;

    pr_info("sunpci: destroying device sunpci%d\n", dev->minor);

    /* Cleanup subsystems */
    sunpci_net_shutdown(dev);
    sunpci_storage_cleanup(dev);

    sunpci_devices[dev->minor] = NULL;
    device_destroy(sunpci_class, MKDEV(sunpci_major, dev->minor));
    cdev_del(&dev->cdev);
    kfree(dev);
}

static int __init sunpci_init(void)
{
    int ret;

    pr_info("sunpci: initializing driver v%d.%d.%d\n",
            SUNPCI_VERSION_MAJOR, SUNPCI_VERSION_MINOR, SUNPCI_VERSION_PATCH);

    /* Allocate device numbers */
    ret = alloc_chrdev_region(&sunpci_devt, 0, SUNPCI_MAX_DEVICES, SUNPCI_DRIVER_NAME);
    if (ret < 0) {
        pr_err("sunpci: failed to allocate chrdev region\n");
        return ret;
    }
    sunpci_major = MAJOR(sunpci_devt);

    /* Create device class */
    sunpci_class = class_create(SUNPCI_DRIVER_NAME);
    if (IS_ERR(sunpci_class)) {
        ret = PTR_ERR(sunpci_class);
        pr_err("sunpci: failed to create class\n");
        goto err_unregister;
    }

    /* Initialize PCI driver - this will probe for hardware */
    ret = sunpci_pci_init();
    if (ret) {
        pr_err("sunpci: no SunPCI hardware found\n");
        goto err_class;
    }

    pr_info("sunpci: driver loaded successfully\n");
    return 0;

err_class:
    class_destroy(sunpci_class);
err_unregister:
    unregister_chrdev_region(sunpci_devt, SUNPCI_MAX_DEVICES);
    return ret;
}

static void __exit sunpci_exit(void)
{
    pr_info("sunpci: unloading driver\n");

    /* Cleanup PCI driver - this will remove all devices */
    sunpci_pci_exit();

    class_destroy(sunpci_class);
    unregister_chrdev_region(sunpci_devt, SUNPCI_MAX_DEVICES);

    pr_info("sunpci: driver unloaded\n");
}

module_init(sunpci_init);
module_exit(sunpci_exit);
