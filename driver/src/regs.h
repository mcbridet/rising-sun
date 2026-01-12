/*
 * SunPCI hardware register definitions
 *
 * The SunPCI card uses an Intel 21554 PCI-to-PCI bridge chip to connect
 * the x86 subsystem to the host SPARC PCI bus. This file defines the
 * register layout for communication with the card.
 *
 * Memory Map (based on analysis of original drivers):
 *
 * BAR0: Primary control registers and mailbox
 * BAR1: Shared memory for IPC ring buffers
 * BAR2: Video framebuffer (when exposed)
 * BAR3: Extended/secondary registers
 *
 * The 21554 provides several mechanisms for cross-bus communication:
 * - Doorbell registers (for interrupts)
 * - Scratchpad registers (for small data exchange)
 * - Translated memory windows (for bulk data)
 */

#ifndef _SUNPCI_REGS_H
#define _SUNPCI_REGS_H

/*
 * Intel 21554 Non-Transparent Bridge Register Offsets
 * These are at fixed offsets in BAR0
 */

/* Configuration Space Offsets (accessible via MMIO) */
#define I21554_CFG_VID              0x00    /* Vendor ID */
#define I21554_CFG_DID              0x02    /* Device ID */
#define I21554_CFG_CMD              0x04    /* Command */
#define I21554_CFG_STS              0x06    /* Status */

/* Primary Interface Registers */
#define I21554_PRI_CSR              0x40    /* Primary CSR (Chip Status) */
#define I21554_PRI_CLR              0x44    /* Primary Clear */
#define I21554_PRI_SET              0x48    /* Primary Set */

/* Secondary Interface Registers */
#define I21554_SEC_CSR              0x4C    /* Secondary CSR */
#define I21554_SEC_CLR              0x50    /* Secondary Clear */
#define I21554_SEC_SET              0x54    /* Secondary Set */

/* Doorbell Registers - for cross-bus interrupts */
#define I21554_PRI_DOORBELL         0x58    /* Primary to Secondary Doorbell */
#define I21554_PRI_DOORBELL_CLR     0x5C    /* Primary Doorbell Clear */
#define I21554_PRI_DOORBELL_MASK    0x60    /* Primary Doorbell Mask */

#define I21554_SEC_DOORBELL         0x64    /* Secondary to Primary Doorbell */
#define I21554_SEC_DOORBELL_CLR     0x68    /* Secondary Doorbell Clear */
#define I21554_SEC_DOORBELL_MASK    0x6C    /* Secondary Doorbell Mask */

/* Scratchpad Registers - for small data exchange */
#define I21554_SCRATCHPAD0          0x80    /* Scratchpad 0 */
#define I21554_SCRATCHPAD1          0x84    /* Scratchpad 1 */
#define I21554_SCRATCHPAD2          0x88    /* Scratchpad 2 */
#define I21554_SCRATCHPAD3          0x8C    /* Scratchpad 3 */
#define I21554_SCRATCHPAD4          0x90    /* Scratchpad 4 */
#define I21554_SCRATCHPAD5          0x94    /* Scratchpad 5 */
#define I21554_SCRATCHPAD6          0x98    /* Scratchpad 6 */
#define I21554_SCRATCHPAD7          0x9C    /* Scratchpad 7 */

/* CSR Bits */
#define I21554_CSR_RESET            (1 << 0)    /* Chip Reset */
#define I21554_CSR_READY            (1 << 1)    /* Ready */
#define I21554_CSR_POWERDOWN        (1 << 2)    /* Power Down */
#define I21554_CSR_LOCK             (1 << 3)    /* Lock */

/* Doorbell Bits - these are defined by SunPCI firmware */
#define SUNPCI_DOORBELL_CMD_READY   (1 << 0)    /* Command queue has data */
#define SUNPCI_DOORBELL_RSP_READY   (1 << 1)    /* Response queue has data */
#define SUNPCI_DOORBELL_VGA_UPDATE  (1 << 2)    /* VGA framebuffer updated */
#define SUNPCI_DOORBELL_RESET       (1 << 7)    /* Guest reset/reboot */

/*
 * SunPCI-Specific Register Layout
 * These are layered on top of the 21554 standard registers
 * Location: Scratchpad registers or dedicated memory region
 */

/* Scratchpad Usage (convention from original driver) */
#define SUNPCI_SCRATCH_VERSION      I21554_SCRATCHPAD0  /* Firmware version */
#define SUNPCI_SCRATCH_STATUS       I21554_SCRATCHPAD1  /* Status flags */
#define SUNPCI_SCRATCH_CMD_HEAD     I21554_SCRATCHPAD2  /* Command queue head */
#define SUNPCI_SCRATCH_CMD_TAIL     I21554_SCRATCHPAD3  /* Command queue tail */
#define SUNPCI_SCRATCH_RSP_HEAD     I21554_SCRATCHPAD4  /* Response queue head */
#define SUNPCI_SCRATCH_RSP_TAIL     I21554_SCRATCHPAD5  /* Response queue tail */
#define SUNPCI_SCRATCH_RESERVED1    I21554_SCRATCHPAD6  /* Reserved */
#define SUNPCI_SCRATCH_RESERVED2    I21554_SCRATCHPAD7  /* Reserved */

/* Status bits (SCRATCH_STATUS) */
#define SUNPCI_STATUS_RUNNING       (1 << 0)    /* x86 guest is running */
#define SUNPCI_STATUS_HALTED        (1 << 1)    /* x86 guest halted */
#define SUNPCI_STATUS_GRAPHICS      (1 << 2)    /* Graphics mode active */
#define SUNPCI_STATUS_NETWORK       (1 << 3)    /* Network initialized */

/*
 * Shared Memory Layout (BAR1)
 *
 * The shared memory region contains the ring buffers and bulk data areas.
 * Layout is (estimated from queue operation analysis):
 *
 * Offset    Size     Purpose
 * 0x00000   0x10000  Command Ring Buffer (64KB)
 * 0x10000   0x10000  Response Ring Buffer (64KB)
 * 0x20000   0x20000  Bulk Data Buffer (128KB)
 * 0x40000   ...      Extended/Variable
 */

#define SUNPCI_SHMEM_CMD_OFFSET     0x00000
#define SUNPCI_SHMEM_CMD_SIZE       0x10000     /* 64KB */

#define SUNPCI_SHMEM_RSP_OFFSET     0x10000
#define SUNPCI_SHMEM_RSP_SIZE       0x10000     /* 64KB */

#define SUNPCI_SHMEM_BULK_OFFSET    0x20000
#define SUNPCI_SHMEM_BULK_SIZE      0x20000     /* 128KB */

#define SUNPCI_SHMEM_MIN_SIZE       (SUNPCI_SHMEM_BULK_OFFSET + SUNPCI_SHMEM_BULK_SIZE)

/*
 * Ring Buffer Entry Header
 */
struct sunpci_ring_header {
    u8  dispatcher_id;      /* Target dispatcher (0=core, 1=vga, 2=video, etc) */
    u8  opcode;             /* Operation within dispatcher */
    u16 length;             /* Total message length including header */
} __packed;

/* Dispatcher IDs */
#define SUNPCI_DISP_CORE    0       /* Core system messages */
#define SUNPCI_DISP_VGA     1       /* VGA/text mode display */
#define SUNPCI_DISP_VIDEO   2       /* High-res video/GDI */
#define SUNPCI_DISP_AUDIO   3       /* Audio (if present) */
#define SUNPCI_DISP_NETWORK 4       /* Network */
#define SUNPCI_DISP_FSD     5       /* Filesystem redirection */
#define SUNPCI_DISP_INPUT   6       /* Keyboard/Mouse input */

/*
 * Helper macros for register access
 */
#define sunpci_read32(dev, offset) \
    readl((dev)->mmio_base + (offset))

#define sunpci_write32(dev, offset, value) \
    writel((value), (dev)->mmio_base + (offset))

#define sunpci_read16(dev, offset) \
    readw((dev)->mmio_base + (offset))

#define sunpci_write16(dev, offset, value) \
    writew((value), (dev)->mmio_base + (offset))

/* For shared memory (BAR1) access */
#define sunpci_shmem_read32(dev, offset) \
    readl((dev)->shmem_base + (offset))

#define sunpci_shmem_write32(dev, offset, value) \
    writel((value), (dev)->shmem_base + (offset))

#endif /* _SUNPCI_REGS_H */
