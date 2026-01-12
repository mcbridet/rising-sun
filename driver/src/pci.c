/*
 * SunPCi driver - PCI device handling
 *
 * This file handles PCI device detection and resource management.
 * The driver only loads when a SunPCi card is detected.
 *
 * SunPCi Hardware:
 *   Uses Intel 21554 PCI-to-PCI Non-Transparent Bridge
 *   Vendor: 0x108e (Sun Microsystems)
 *   Device: 0x5043 ("PC" in ASCII)
 *
 * PCI BARs:
 *   BAR0: Control registers (MMIO)
 *   BAR1: Shared memory for IPC ring buffers
 *   BAR2: Video framebuffer (optional)
 *   BAR3: Extended registers (optional)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "sunpci.h"
#include "regs.h"

static int sunpci_card_count = 0;

/* PCI device table - driver only binds to actual SunPCi hardware */
static const struct pci_device_id sunpci_pci_ids[] = {
    { PCI_DEVICE(SUNPCI_VENDOR_ID, SUNPCI_DEVICE_ID) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, sunpci_pci_ids);

/**
 * sunpci_irq_handler - Interrupt handler
 * @irq: IRQ number
 * @dev_id: Device pointer
 *
 * Handles interrupts from the SunPCi card (doorbell events).
 */
static irqreturn_t sunpci_irq_handler(int irq, void *dev_id)
{
    struct sunpci_device *dev = dev_id;
    u32 doorbell;

    if (!dev->mmio_base)
        return IRQ_NONE;

    /* Read and clear doorbell status */
    doorbell = sunpci_read32(dev, I21554_SEC_DOORBELL);
    if (doorbell == 0)
        return IRQ_NONE;

    /* Clear the bits we're handling */
    sunpci_write32(dev, I21554_SEC_DOORBELL_CLR, doorbell);

    /* Handle doorbell events */
    if (doorbell & SUNPCI_DOORBELL_RSP_READY) {
        /* Response data available - could wake up waiters */
        pr_debug("sunpci%d: response ready\n", dev->minor);
    }

    if (doorbell & SUNPCI_DOORBELL_VGA_UPDATE) {
        /* VGA framebuffer was updated */
        pr_debug("sunpci%d: VGA update\n", dev->minor);
    }

    if (doorbell & SUNPCI_DOORBELL_RESET) {
        /* Guest initiated reset */
        pr_info("sunpci%d: guest reset\n", dev->minor);
    }

    return IRQ_HANDLED;
}

/**
 * sunpci_setup_rings - Initialize ring buffers from shared memory
 * @dev: SunPCi device
 *
 * Sets up command and response ring buffers in shared memory.
 */
static int sunpci_setup_rings(struct sunpci_device *dev)
{
    int ret;

    if (!dev->shmem_base || dev->shmem_len < SUNPCI_SHMEM_MIN_SIZE) {
        pr_warn("sunpci%d: shared memory too small for rings (%llu < %d)\n",
                dev->minor, (unsigned long long)dev->shmem_len,
                SUNPCI_SHMEM_MIN_SIZE);
        return -ENOMEM;
    }

    /* Command ring: host writes, guest reads */
    ret = sunpci_ring_init(&dev->cmd_ring,
                           dev->shmem_base + SUNPCI_SHMEM_CMD_OFFSET,
                           0, /* Physical addr not needed for MMIO */
                           SUNPCI_SHMEM_CMD_SIZE);
    if (ret) {
        pr_err("sunpci%d: failed to init command ring: %d\n", dev->minor, ret);
        return ret;
    }

    /* Set up hardware-managed pointers */
    dev->cmd_ring.hw_managed = true;
    dev->cmd_ring.head_reg = SUNPCI_SCRATCH_CMD_HEAD;
    dev->cmd_ring.tail_reg = SUNPCI_SCRATCH_CMD_TAIL;

    /* Response ring: guest writes, host reads */
    ret = sunpci_ring_init(&dev->rsp_ring,
                           dev->shmem_base + SUNPCI_SHMEM_RSP_OFFSET,
                           0,
                           SUNPCI_SHMEM_RSP_SIZE);
    if (ret) {
        pr_err("sunpci%d: failed to init response ring: %d\n", dev->minor, ret);
        return ret;
    }

    dev->rsp_ring.hw_managed = true;
    dev->rsp_ring.head_reg = SUNPCI_SCRATCH_RSP_HEAD;
    dev->rsp_ring.tail_reg = SUNPCI_SCRATCH_RSP_TAIL;

    pr_info("sunpci%d: ring buffers initialized (cmd=%uB, rsp=%uB)\n",
            dev->minor, SUNPCI_SHMEM_CMD_SIZE, SUNPCI_SHMEM_RSP_SIZE);

    return 0;
}

/**
 * sunpci_pci_probe - PCI device probe callback
 * @pdev: PCI device being probed
 * @id: PCI device ID entry that matched
 *
 * Called when a SunPCi card is detected. Sets up resources and
 * creates the character device interface.
 */
static int sunpci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct sunpci_device *dev;
    int ret;
    int i;

    pr_info("sunpci: found SunPCi card at %s\n", pci_name(pdev));

    /* Log all BARs for debugging */
    for (i = 0; i < 6; i++) {
        if (pci_resource_len(pdev, i) > 0) {
            pr_info("sunpci:   BAR%d: %pR\n", i, &pdev->resource[i]);
        }
    }

    /* Enable the PCI device */
    ret = pci_enable_device(pdev);
    if (ret) {
        pr_err("sunpci: failed to enable PCI device: %d\n", ret);
        return ret;
    }

    /* Request MMIO regions */
    ret = pci_request_regions(pdev, SUNPCI_DRIVER_NAME);
    if (ret) {
        pr_err("sunpci: failed to request PCI regions: %d\n", ret);
        goto err_disable;
    }

    /* Set bus master for DMA */
    pci_set_master(pdev);

    /* Create the sunpci device */
    dev = sunpci_create_device(sunpci_card_count, pdev);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        pr_err("sunpci: failed to create device: %d\n", ret);
        goto err_regions;
    }

    dev->pdev = pdev;

    /* Map BAR0: Control registers */
    dev->mmio_len = pci_resource_len(pdev, 0);
    if (dev->mmio_len > 0) {
        dev->mmio_base = pci_iomap(pdev, 0, 0);
        if (!dev->mmio_base) {
            pr_err("sunpci: failed to map BAR0\n");
            ret = -ENOMEM;
            goto err_device;
        }
        pr_info("sunpci:   BAR0 mapped at %p (%llu bytes)\n",
                dev->mmio_base, (unsigned long long)dev->mmio_len);
    }

    /* Map BAR1: Shared memory */
    dev->shmem_len = pci_resource_len(pdev, 1);
    if (dev->shmem_len > 0) {
        dev->shmem_base = pci_iomap(pdev, 1, 0);
        if (!dev->shmem_base) {
            pr_warn("sunpci: failed to map BAR1 (shared memory)\n");
        } else {
            pr_info("sunpci:   BAR1 mapped at %p (%llu bytes)\n",
                    dev->shmem_base, (unsigned long long)dev->shmem_len);
        }
    }

    /* Read hardware version from scratchpad */
    if (dev->mmio_base) {
        dev->hw_version = sunpci_read32(dev, SUNPCI_SCRATCH_VERSION);
        pr_info("sunpci:   hardware version: 0x%08x\n", dev->hw_version);
    }

    /* Set up ring buffers */
    if (dev->shmem_base) {
        ret = sunpci_setup_rings(dev);
        if (ret)
            pr_warn("sunpci: ring buffer setup failed, continuing without IPC\n");
    }

    /* Request IRQ */
    if (pdev->irq) {
        ret = request_irq(pdev->irq, sunpci_irq_handler, IRQF_SHARED,
                          SUNPCI_DRIVER_NAME, dev);
        if (ret) {
            pr_warn("sunpci: failed to request IRQ %d: %d\n", pdev->irq, ret);
        } else {
            dev->irq = pdev->irq;
            pr_info("sunpci:   using IRQ %d\n", dev->irq);

            /* Enable doorbell interrupts */
            if (dev->mmio_base) {
                sunpci_write32(dev, I21554_SEC_DOORBELL_MASK, 0xFFFFFFFF);
            }
        }
    }

    pci_set_drvdata(pdev, dev);
    sunpci_card_count++;

    pr_info("sunpci: card %d initialized successfully\n", dev->minor);
    return 0;

err_device:
    sunpci_destroy_device(dev);
err_regions:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

/**
 * sunpci_pci_remove - PCI device remove callback
 * @pdev: PCI device being removed
 *
 * Called when a SunPCi card is removed or driver is unloaded.
 */
static void sunpci_pci_remove(struct pci_dev *pdev)
{
    struct sunpci_device *dev = pci_get_drvdata(pdev);

    pr_info("sunpci: removing card at %s\n", pci_name(pdev));

    if (dev) {
        /* Disable interrupts */
        if (dev->mmio_base)
            sunpci_write32(dev, I21554_SEC_DOORBELL_MASK, 0);

        /* Free IRQ */
        if (dev->irq)
            free_irq(dev->irq, dev);

        /* Unmap BARs */
        if (dev->shmem_base)
            pci_iounmap(pdev, dev->shmem_base);
        if (dev->mmio_base)
            pci_iounmap(pdev, dev->mmio_base);

        sunpci_destroy_device(dev);
    }

    pci_release_regions(pdev);
    pci_disable_device(pdev);
    sunpci_card_count--;
}

#ifdef CONFIG_PM_SLEEP
/**
 * sunpci_pci_suspend - PCI device suspend callback
 * @dev: Device being suspended
 *
 * Called when the system enters a sleep state. We need to:
 * - Stop DMA/IPC activity
 * - Disable interrupts
 * - Save any volatile hardware state
 */
static int sunpci_pci_suspend(struct device *device)
{
    struct pci_dev *pdev = to_pci_dev(device);
    struct sunpci_device *dev = pci_get_drvdata(pdev);

    pr_info("sunpci%d: suspending\n", dev->minor);

    /* Mark device as suspended */
    dev->suspended = true;

    /* Disable doorbell interrupts */
    if (dev->mmio_base)
        sunpci_write32(dev, I21554_SEC_DOORBELL_MASK, 0);

    /* Synchronize IRQ - ensure handler isn't running */
    if (dev->irq)
        synchronize_irq(dev->irq);

    /* Notify guest of impending suspend via doorbell (if supported) */
    if (dev->mmio_base)
        sunpci_write32(dev, I21554_PRI_DOORBELL, SUNPCI_DOORBELL_RESET);

    pr_debug("sunpci%d: suspended\n", dev->minor);
    return 0;
}

/**
 * sunpci_pci_resume - PCI device resume callback
 * @dev: Device being resumed
 *
 * Called when the system wakes from a sleep state. We need to:
 * - Restore hardware state
 * - Re-enable interrupts
 * - Resume IPC activity
 */
static int sunpci_pci_resume(struct device *device)
{
    struct pci_dev *pdev = to_pci_dev(device);
    struct sunpci_device *dev = pci_get_drvdata(pdev);

    pr_info("sunpci%d: resuming\n", dev->minor);

    /* Re-enable doorbell interrupts */
    if (dev->mmio_base)
        sunpci_write32(dev, I21554_SEC_DOORBELL_MASK, 0xFFFFFFFF);

    /* Clear any pending doorbells that accumulated during suspend */
    if (dev->mmio_base) {
        u32 pending = sunpci_read32(dev, I21554_SEC_DOORBELL);
        if (pending)
            sunpci_write32(dev, I21554_SEC_DOORBELL_CLR, pending);
    }

    /* Mark device as active */
    dev->suspended = false;

    pr_debug("sunpci%d: resumed\n", dev->minor);
    return 0;
}

static SIMPLE_DEV_PM_OPS(sunpci_pm_ops, sunpci_pci_suspend, sunpci_pci_resume);
#define SUNPCI_PM_OPS (&sunpci_pm_ops)
#else
#define SUNPCI_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver sunpci_pci_driver = {
    .name = SUNPCI_DRIVER_NAME,
    .id_table = sunpci_pci_ids,
    .probe = sunpci_pci_probe,
    .remove = sunpci_pci_remove,
    .driver.pm = SUNPCI_PM_OPS,
};

int sunpci_pci_init(void)
{
    int ret;

    ret = pci_register_driver(&sunpci_pci_driver);
    if (ret) {
        pr_err("sunpci: failed to register PCI driver: %d\n", ret);
        return ret;
    }

    /* Check if any devices were found */
    if (sunpci_card_count == 0) {
        pr_err("sunpci: no SunPCi cards detected\n");
        pci_unregister_driver(&sunpci_pci_driver);
        return -ENODEV;
    }

    pr_info("sunpci: registered PCI driver, %d card(s) found\n", sunpci_card_count);
    return 0;
}

void sunpci_pci_exit(void)
{
    pci_unregister_driver(&sunpci_pci_driver);
    pr_debug("sunpci: PCI driver unregistered\n");
}
