/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SunPCI driver - Ring buffer definitions
 */

#ifndef _SUNPCI_RING_H
#define _SUNPCI_RING_H

#include <linux/types.h>
#include <linux/spinlock.h>

/* Forward declaration */
struct sunpci_device;

/**
 * struct sunpci_ring - Ring buffer descriptor
 * @base: Virtual address of ring buffer memory
 * @phys: Physical address (for DMA)
 * @size: Total size of ring buffer in bytes
 * @head: Producer index (where to write next)
 * @tail: Consumer index (where to read next)
 * @lock: Spinlock for synchronization
 * @head_reg: MMIO offset for head pointer (if hardware-managed)
 * @tail_reg: MMIO offset for tail pointer (if hardware-managed)
 *
 * The ring buffer uses a simple producer-consumer model:
 * - Producer advances head after writing
 * - Consumer advances tail after reading
 * - Buffer is empty when head == tail
 * - Buffer is full when (head + 1) % size == tail
 */
struct sunpci_ring {
    void __iomem *base;
    dma_addr_t phys;
    u32 size;
    u32 head;
    u32 tail;
    spinlock_t lock;
    
    /* For hardware-managed pointers */
    u32 head_reg;
    u32 tail_reg;
    bool hw_managed;
};

/* Ring buffer operations */

/**
 * sunpci_ring_init - Initialize a ring buffer
 * @ring: Ring buffer to initialize
 * @base: Virtual address of buffer memory
 * @phys: Physical address
 * @size: Size in bytes (must be power of 2)
 *
 * Returns 0 on success, negative error on failure.
 */
int sunpci_ring_init(struct sunpci_ring *ring, void __iomem *base,
                     dma_addr_t phys, u32 size);

/**
 * sunpci_ring_reset - Reset ring buffer to empty state
 * @ring: Ring buffer to reset
 */
void sunpci_ring_reset(struct sunpci_ring *ring);

/**
 * sunpci_ring_space - Get available space for writing
 * @ring: Ring buffer
 *
 * Returns number of bytes available for writing.
 */
u32 sunpci_ring_space(struct sunpci_ring *ring);

/**
 * sunpci_ring_used - Get used space (data available for reading)
 * @ring: Ring buffer
 *
 * Returns number of bytes available for reading.
 */
u32 sunpci_ring_used(struct sunpci_ring *ring);

/**
 * sunpci_ring_write - Write data to ring buffer
 * @ring: Ring buffer
 * @data: Data to write
 * @len: Length of data
 *
 * Returns number of bytes written, or negative error.
 */
int sunpci_ring_write(struct sunpci_ring *ring, const void *data, u32 len);

/**
 * sunpci_ring_read - Read data from ring buffer
 * @ring: Ring buffer
 * @data: Buffer to read into
 * @len: Maximum bytes to read
 *
 * Returns number of bytes read, or negative error.
 */
int sunpci_ring_read(struct sunpci_ring *ring, void *data, u32 len);

/**
 * sunpci_ring_peek - Peek at data without consuming
 * @ring: Ring buffer
 * @data: Buffer to read into
 * @len: Maximum bytes to peek
 *
 * Returns number of bytes peeked, or negative error.
 */
int sunpci_ring_peek(struct sunpci_ring *ring, void *data, u32 len);

/**
 * sunpci_ring_skip - Skip data (advance tail without reading)
 * @ring: Ring buffer
 * @len: Bytes to skip
 *
 * Returns 0 on success, negative error on failure.
 */
int sunpci_ring_skip(struct sunpci_ring *ring, u32 len);

/**
 * sunpci_ring_sync_head - Sync head pointer with hardware
 * @ring: Ring buffer
 * @dev: SunPCI device (for MMIO access)
 *
 * For hardware-managed rings, reads head from MMIO register.
 */
void sunpci_ring_sync_head(struct sunpci_ring *ring, struct sunpci_device *dev);

/**
 * sunpci_ring_sync_tail - Sync tail pointer with hardware
 * @ring: Ring buffer
 * @dev: SunPCI device (for MMIO access)
 *
 * For hardware-managed rings, writes tail to MMIO register.
 */
void sunpci_ring_sync_tail(struct sunpci_ring *ring, struct sunpci_device *dev);

#endif /* _SUNPCI_RING_H */
