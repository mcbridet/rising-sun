// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Memory mapping support
 *
 * Provides mmap access to the framebuffer and shared memory for userspace.
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/io.h>

#include "sunpci.h"
#include "regs.h"

/*
 * Memory regions that can be mapped by userspace
 *
 * We use the vm_pgoff to select which region to map:
 *   pgoff == 0: Framebuffer (BAR2)
 *   pgoff == 1: Shared memory (BAR1) - for advanced users
 */
#define SUNPCI_MMAP_FRAMEBUFFER  0
#define SUNPCI_MMAP_SHMEM        1

/**
 * sunpci_mmap - Map device memory to userspace
 * @file: File handle
 * @vma: Virtual memory area to populate
 *
 * Allows userspace to directly access the framebuffer for efficient
 * display rendering without copying.
 */
int sunpci_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct sunpci_device *dev = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;
    resource_size_t phys_start;
    resource_size_t region_size;
    int ret;

    if (!dev || !dev->pdev)
        return -ENODEV;

    switch (vma->vm_pgoff) {
    case SUNPCI_MMAP_FRAMEBUFFER:
        /*
         * Map the framebuffer (BAR2) for display access
         * This is the primary use case - allows userspace to read
         * pixel data for rendering in a Qt/GTK window.
         */
        if (!dev->display.framebuffer.phys_addr_lo && !dev->display.framebuffer.phys_addr_hi) {
            /* Try to get BAR2 address from PCI config */
            phys_start = pci_resource_start(dev->pdev, 2);
            region_size = pci_resource_len(dev->pdev, 2);
        } else {
            /* Use previously set framebuffer info */
            phys_start = ((u64)dev->display.framebuffer.phys_addr_hi << 32) |
                        dev->display.framebuffer.phys_addr_lo;
            region_size = ((u64)dev->display.framebuffer.size_hi << 32) |
                         dev->display.framebuffer.size_lo;
        }
        break;

    case SUNPCI_MMAP_SHMEM:
        /*
         * Map shared memory (BAR1) for direct IPC access
         * This is for advanced usage - normally IPC goes through
         * the kernel ring buffers.
         */
        phys_start = pci_resource_start(dev->pdev, 1);
        region_size = pci_resource_len(dev->pdev, 1);
        break;

    default:
        dev_warn(&dev->pdev->dev, "mmap: invalid region %lu\n", vma->vm_pgoff);
        return -EINVAL;
    }

    /* Validate region exists */
    if (phys_start == 0 || region_size == 0) {
        dev_warn(&dev->pdev->dev, "mmap: region %lu not available\n",
                 vma->vm_pgoff);
        return -ENODEV;
    }

    /* Validate size fits in region */
    if (size > region_size) {
        dev_warn(&dev->pdev->dev, "mmap: requested size %lu > region size %llu\n",
                 size, (unsigned long long)region_size);
        return -EINVAL;
    }

    /* Set up mapping as uncached/write-combining for MMIO */
    vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
    
    /* Use write-combining for framebuffer, uncached for shared memory */
    if (vma->vm_pgoff == SUNPCI_MMAP_FRAMEBUFFER) {
        vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    } else {
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    }

    /* Map the physical memory to userspace */
    pfn = phys_start >> PAGE_SHIFT;
    ret = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
    if (ret) {
        dev_err(&dev->pdev->dev, "mmap: remap_pfn_range failed: %d\n", ret);
        return ret;
    }

    dev_dbg(&dev->pdev->dev, "mmap: mapped region %lu, phys 0x%llx, size %lu\n",
            vma->vm_pgoff, (unsigned long long)phys_start, size);

    return 0;
}

/**
 * sunpci_get_fb_info - Get framebuffer info for userspace
 * @dev: Device
 * @info: Output info structure
 *
 * Called by ioctl to provide framebuffer details to userspace.
 */
int sunpci_get_fb_info(struct sunpci_device *dev, struct sunpci_framebuffer *info)
{
    resource_size_t fb_start, fb_len;

    if (!dev || !dev->pdev)
        return -ENODEV;

    fb_start = pci_resource_start(dev->pdev, 2);
    fb_len = pci_resource_len(dev->pdev, 2);

    if (fb_start == 0 || fb_len == 0) {
        /* BAR2 not available */
        memset(info, 0, sizeof(*info));
        return -ENODEV;
    }

    info->phys_addr_lo = (u32)fb_start;
    info->phys_addr_hi = (u32)(fb_start >> 32);
    info->size_lo = (u32)fb_len;
    info->size_hi = (u32)(fb_len >> 32);
    info->stride = dev->display.info.width * (dev->display.info.color_depth / 8);
    if (info->stride == 0) {
        /* Default assumption: 32bpp, 1024 wide */
        info->stride = 1024 * 4;
    }

    /* Format based on color depth */
    switch (dev->display.info.color_depth) {
    case 8:
        info->format = 0; /* Indexed */
        break;
    case 15:
    case 16:
        info->format = 1; /* RGB565 */
        break;
    case 24:
        info->format = 2; /* RGB888 */
        break;
    case 32:
    default:
        info->format = 3; /* XRGB8888 */
        break;
    }

    return 0;
}
