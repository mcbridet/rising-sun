/*
 * SunPCI IPC Protocol Implementation
 *
 * Handles message send/receive via ring buffers with the guest.
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "sunpci.h"
#include "regs.h"
#include "ipc.h"

/* Sequence number for message tracking */
static atomic_t ipc_sequence = ATOMIC_INIT(0);

/*
 * Get next sequence number
 */
static u32 sunpci_ipc_next_seq(void)
{
    return atomic_inc_return(&ipc_sequence);
}

/*
 * Ring doorbell to notify guest
 */
static void sunpci_ring_doorbell(struct sunpci_device *dev, u32 bits)
{
    if (!dev->mmio_base)
        return;
    
    /* Write to primary doorbell register to trigger interrupt on guest (secondary side) */
    iowrite32(bits, dev->mmio_base + I21554_PRI_DOORBELL);
}

/*
 * Send a command to the guest via the command ring
 *
 * @dev: Device context
 * @dispatcher: Target dispatcher ID (SUNPCI_DISP_*)
 * @command: Command code for dispatcher
 * @payload: Optional payload data
 * @payload_len: Length of payload
 * @seq_out: Output sequence number for matching response
 *
 * Returns: 0 on success, negative errno on failure
 */
int sunpci_ipc_send_cmd(struct sunpci_device *dev,
                        u16 dispatcher, u16 command,
                        const void *payload, size_t payload_len,
                        u32 *seq_out)
{
    struct sunpci_msg_header hdr;
    size_t total_len;
    u32 seq;
    int ret;

    if (!dev || !dev->cmd_ring.base)
        return -EINVAL;

    if (payload_len > SUNPCI_MAX_PAYLOAD)
        return -EINVAL;

    total_len = SUNPCI_MSG_HDR_SIZE + payload_len;

    /* Build message header */
    seq = sunpci_ipc_next_seq();
    hdr.magic = cpu_to_le32(SUNPCI_MSG_MAGIC);
    hdr.dispatcher = cpu_to_le16(dispatcher);
    hdr.command = cpu_to_le16(command);
    hdr.sequence = cpu_to_le32(seq);
    hdr.payload_len = cpu_to_le32(payload_len);

    /* Check ring has space */
    if (sunpci_ring_space(&dev->cmd_ring) < total_len) {
        dev_warn(&dev->pdev->dev, "cmd ring full\n");
        return -ENOSPC;
    }

    /* Write header */
    ret = sunpci_ring_write(&dev->cmd_ring, &hdr, sizeof(hdr));
    if (ret < 0)
        return ret;

    /* Write payload if present */
    if (payload && payload_len > 0) {
        ret = sunpci_ring_write(&dev->cmd_ring, payload, payload_len);
        if (ret < 0)
            return ret;
    }

    /* Sync tail pointer to hardware */
    sunpci_ring_sync_tail(&dev->cmd_ring, dev);

    /* Ring doorbell to notify guest */
    sunpci_ring_doorbell(dev, SUNPCI_DOORBELL_CMD_READY);

    if (seq_out)
        *seq_out = seq;

    return 0;
}

/*
 * Receive a response from the response ring
 *
 * @dev: Device context
 * @expected_seq: Expected sequence number (0 = any)
 * @status_out: Output status code
 * @payload: Buffer for response payload
 * @payload_len: Size of payload buffer
 * @actual_len: Actual payload length received
 * @timeout: Timeout in jiffies (0 = no wait)
 *
 * Returns: 0 on success, negative errno on failure
 */
int sunpci_ipc_recv_rsp(struct sunpci_device *dev,
                        u32 expected_seq,
                        u16 *status_out,
                        void *payload, size_t payload_len,
                        size_t *actual_len,
                        unsigned long timeout)
{
    struct sunpci_rsp_header hdr;
    unsigned long deadline;
    size_t copy_len;
    int ret;

    if (!dev || !dev->rsp_ring.base)
        return -EINVAL;

    deadline = jiffies + timeout;

    do {
        /* Sync head pointer from hardware */
        sunpci_ring_sync_head(&dev->rsp_ring, dev);

        /* Check if we have a complete header */
        if (sunpci_ring_used(&dev->rsp_ring) >= sizeof(hdr)) {
            /* Peek at header without consuming */
            ret = sunpci_ring_peek(&dev->rsp_ring, &hdr, sizeof(hdr));
            if (ret < 0)
                return ret;

            /* Validate magic */
            if (le32_to_cpu(hdr.magic) != SUNPCI_MSG_MAGIC) {
                dev_err(&dev->pdev->dev, "bad response magic: 0x%08x\n",
                        le32_to_cpu(hdr.magic));
                /* Try to recover by skipping a byte */
                sunpci_ring_skip(&dev->rsp_ring, 1);
                continue;
            }

            /* Check sequence if filtering */
            if (expected_seq && le32_to_cpu(hdr.sequence) != expected_seq) {
                /* Not our response, skip it */
                sunpci_ring_skip(&dev->rsp_ring,
                                sizeof(hdr) + le32_to_cpu(hdr.payload_len));
                continue;
            }

            /* Have full message? */
            if (sunpci_ring_used(&dev->rsp_ring) >=
                sizeof(hdr) + le32_to_cpu(hdr.payload_len)) {
                
                /* Consume header */
                sunpci_ring_skip(&dev->rsp_ring, sizeof(hdr));

                /* Read payload */
                copy_len = min(payload_len,
                              (size_t)le32_to_cpu(hdr.payload_len));
                if (copy_len > 0 && payload) {
                    ret = sunpci_ring_read(&dev->rsp_ring,
                                          payload, copy_len);
                    if (ret < 0)
                        return ret;
                }

                /* Skip any remaining payload we didn't read */
                if (le32_to_cpu(hdr.payload_len) > copy_len) {
                    sunpci_ring_skip(&dev->rsp_ring,
                                    le32_to_cpu(hdr.payload_len) - copy_len);
                }

                /* Sync head back to hardware */
                sunpci_ring_sync_head(&dev->rsp_ring, dev);

                /* Return results */
                if (status_out)
                    *status_out = le16_to_cpu(hdr.status);
                if (actual_len)
                    *actual_len = le32_to_cpu(hdr.payload_len);

                return 0;
            }
        }

        /* No response yet, wait a bit if we have timeout remaining */
        if (timeout && time_before(jiffies, deadline)) {
            usleep_range(100, 500);
        }

    } while (timeout && time_before(jiffies, deadline));

    return timeout ? -ETIMEDOUT : -EAGAIN;
}

/*
 * Send command and wait for response (synchronous)
 *
 * @dev: Device context
 * @dispatcher: Target dispatcher ID
 * @command: Command code
 * @cmd_payload: Command payload
 * @cmd_len: Command payload length
 * @rsp_payload: Response payload buffer
 * @rsp_len: Response buffer size
 * @actual_rsp_len: Actual response length
 * @timeout: Timeout in jiffies
 *
 * Returns: 0 on success, negative errno on failure
 */
int sunpci_ipc_transact(struct sunpci_device *dev,
                        u16 dispatcher, u16 command,
                        const void *cmd_payload, size_t cmd_len,
                        void *rsp_payload, size_t rsp_len,
                        size_t *actual_rsp_len,
                        unsigned long timeout)
{
    u32 seq;
    u16 status;
    int ret;

    /* Send command */
    ret = sunpci_ipc_send_cmd(dev, dispatcher, command,
                              cmd_payload, cmd_len, &seq);
    if (ret < 0)
        return ret;

    /* Wait for response */
    ret = sunpci_ipc_recv_rsp(dev, seq, &status,
                              rsp_payload, rsp_len, actual_rsp_len,
                              timeout);
    if (ret < 0)
        return ret;

    /* Check status */
    if (status != SUNPCI_RSP_SUCCESS) {
        dev_dbg(&dev->pdev->dev, "command failed: dispatcher=%d cmd=%d status=%d\n",
                dispatcher, command, status);
        return -EIO;
    }

    return 0;
}

/*
 * Initialize communication with guest
 */
int sunpci_ipc_init(struct sunpci_device *dev)
{
    struct sunpci_core_init cmd;
    struct sunpci_core_init_rsp rsp;
    size_t rsp_len;
    int ret;

    cmd.host_version = cpu_to_le32(0x00010000); /* Version 1.0 */
    cmd.features_supported = cpu_to_le32(0xFFFFFFFF); /* All features */

    ret = sunpci_ipc_transact(dev, SUNPCI_DISP_CORE, CORE_CMD_INIT,
                              &cmd, sizeof(cmd),
                              &rsp, sizeof(rsp), &rsp_len,
                              SUNPCI_INIT_TIMEOUT);
    if (ret < 0) {
        dev_warn(&dev->pdev->dev, "guest init failed: %d\n", ret);
        return ret;
    }

    dev_info(&dev->pdev->dev, "guest version 0x%08x, features 0x%08x\n",
             le32_to_cpu(rsp.guest_version),
             le32_to_cpu(rsp.features_enabled));

    return 0;
}

/*
 * Shutdown communication with guest
 */
void sunpci_ipc_shutdown(struct sunpci_device *dev)
{
    /* Send shutdown command - don't wait for response */
    sunpci_ipc_send_cmd(dev, SUNPCI_DISP_CORE, CORE_CMD_SHUTDOWN,
                        NULL, 0, NULL);
}

/*
 * Process pending responses in interrupt context
 *
 * Called from IRQ handler when we receive RSP_READY doorbell.
 * For now, just wake up any waiting readers.
 */
void sunpci_ipc_handle_responses(struct sunpci_device *dev)
{
    /* Sync ring head from hardware */
    sunpci_ring_sync_head(&dev->rsp_ring, dev);

    /* TODO: Wake up waiters */
    /* For now, responses are polled in sunpci_ipc_recv_rsp */
}
