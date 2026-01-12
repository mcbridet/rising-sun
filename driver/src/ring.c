// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Ring buffer implementation
 *
 * This implements the ring buffer queues used for IPC between the
 * host and the SunPCI card's x86 guest.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/string.h>

#include "sunpci.h"
#include "ring.h"
#include "regs.h"

/**
 * is_power_of_2 check for ring size
 */
static inline bool ring_size_valid(u32 size)
{
    return size >= 64 && (size & (size - 1)) == 0;
}

int sunpci_ring_init(struct sunpci_ring *ring, void __iomem *base,
                     dma_addr_t phys, u32 size)
{
    if (!ring || !base)
        return -EINVAL;
        
    if (!ring_size_valid(size)) {
        pr_err("sunpci: ring size must be power of 2 >= 64 (got %u)\n", size);
        return -EINVAL;
    }

    memset(ring, 0, sizeof(*ring));
    ring->base = base;
    ring->phys = phys;
    ring->size = size;
    ring->head = 0;
    ring->tail = 0;
    ring->hw_managed = false;
    spin_lock_init(&ring->lock);

    return 0;
}

void sunpci_ring_reset(struct sunpci_ring *ring)
{
    unsigned long flags;

    spin_lock_irqsave(&ring->lock, flags);
    ring->head = 0;
    ring->tail = 0;
    spin_unlock_irqrestore(&ring->lock, flags);
}

u32 sunpci_ring_space(struct sunpci_ring *ring)
{
    u32 head, tail;
    
    head = READ_ONCE(ring->head);
    tail = READ_ONCE(ring->tail);
    
    /* Leave one slot empty to distinguish full from empty */
    if (head >= tail)
        return ring->size - (head - tail) - 1;
    else
        return tail - head - 1;
}

u32 sunpci_ring_used(struct sunpci_ring *ring)
{
    u32 head, tail;
    
    head = READ_ONCE(ring->head);
    tail = READ_ONCE(ring->tail);
    
    if (head >= tail)
        return head - tail;
    else
        return ring->size - (tail - head);
}

int sunpci_ring_write(struct sunpci_ring *ring, const void *data, u32 len)
{
    unsigned long flags;
    u32 space, head, chunk1, chunk2;
    const u8 *src = data;

    if (!data || len == 0)
        return -EINVAL;

    spin_lock_irqsave(&ring->lock, flags);

    space = sunpci_ring_space(ring);
    if (len > space) {
        spin_unlock_irqrestore(&ring->lock, flags);
        return -ENOSPC;
    }

    head = ring->head;

    /* Handle wrap-around */
    if (head + len <= ring->size) {
        /* No wrap */
        memcpy_toio(ring->base + head, src, len);
    } else {
        /* Wrap around */
        chunk1 = ring->size - head;
        chunk2 = len - chunk1;
        memcpy_toio(ring->base + head, src, chunk1);
        memcpy_toio(ring->base, src + chunk1, chunk2);
    }

    /* Update head with memory barrier */
    smp_wmb();
    WRITE_ONCE(ring->head, (head + len) % ring->size);

    spin_unlock_irqrestore(&ring->lock, flags);
    return len;
}

int sunpci_ring_read(struct sunpci_ring *ring, void *data, u32 len)
{
    unsigned long flags;
    u32 used, tail, chunk1, chunk2;
    u8 *dst = data;

    if (!data || len == 0)
        return -EINVAL;

    spin_lock_irqsave(&ring->lock, flags);

    used = sunpci_ring_used(ring);
    if (len > used)
        len = used;

    if (len == 0) {
        spin_unlock_irqrestore(&ring->lock, flags);
        return 0;
    }

    tail = ring->tail;

    /* Handle wrap-around */
    if (tail + len <= ring->size) {
        /* No wrap */
        memcpy_fromio(dst, ring->base + tail, len);
    } else {
        /* Wrap around */
        chunk1 = ring->size - tail;
        chunk2 = len - chunk1;
        memcpy_fromio(dst, ring->base + tail, chunk1);
        memcpy_fromio(dst + chunk1, ring->base, chunk2);
    }

    /* Update tail with memory barrier */
    smp_rmb();
    WRITE_ONCE(ring->tail, (tail + len) % ring->size);

    spin_unlock_irqrestore(&ring->lock, flags);
    return len;
}

int sunpci_ring_peek(struct sunpci_ring *ring, void *data, u32 len)
{
    unsigned long flags;
    u32 used, tail, chunk1, chunk2;
    u8 *dst = data;

    if (!data || len == 0)
        return -EINVAL;

    spin_lock_irqsave(&ring->lock, flags);

    used = sunpci_ring_used(ring);
    if (len > used)
        len = used;

    if (len == 0) {
        spin_unlock_irqrestore(&ring->lock, flags);
        return 0;
    }

    tail = ring->tail;

    /* Handle wrap-around */
    if (tail + len <= ring->size) {
        memcpy_fromio(dst, ring->base + tail, len);
    } else {
        chunk1 = ring->size - tail;
        chunk2 = len - chunk1;
        memcpy_fromio(dst, ring->base + tail, chunk1);
        memcpy_fromio(dst + chunk1, ring->base, chunk2);
    }

    /* Don't update tail for peek */
    spin_unlock_irqrestore(&ring->lock, flags);
    return len;
}

int sunpci_ring_skip(struct sunpci_ring *ring, u32 len)
{
    unsigned long flags;
    u32 used;

    spin_lock_irqsave(&ring->lock, flags);

    used = sunpci_ring_used(ring);
    if (len > used) {
        spin_unlock_irqrestore(&ring->lock, flags);
        return -EINVAL;
    }

    WRITE_ONCE(ring->tail, (ring->tail + len) % ring->size);

    spin_unlock_irqrestore(&ring->lock, flags);
    return 0;
}

void sunpci_ring_sync_head(struct sunpci_ring *ring, struct sunpci_device *dev)
{
    if (ring->hw_managed && dev->mmio_base) {
        ring->head = sunpci_read32(dev, ring->head_reg);
    }
}

void sunpci_ring_sync_tail(struct sunpci_ring *ring, struct sunpci_device *dev)
{
    if (ring->hw_managed && dev->mmio_base) {
        sunpci_write32(dev, ring->tail_reg, ring->tail);
    }
}
