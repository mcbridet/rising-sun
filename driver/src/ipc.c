/*
 * SunPCi IPC Protocol Implementation
 *
 * Handles message send/receive via ring buffers with the guest.
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "sunpci.h"
#include "regs.h"
#include "ring.h"
#include "ipc.h"

/* Forward declarations for static dispatch helpers */
static void sunpci_dispatch_storage(struct sunpci_device *dev,
                                    u16 command, u32 sequence,
                                    void *payload, size_t payload_len);
static void sunpci_dispatch_network(struct sunpci_device *dev,
                                    u16 command, u32 sequence,
                                    void *payload, size_t payload_len);
static void sunpci_dispatch_fsd(struct sunpci_device *dev,
                                u16 command, u32 sequence,
                                void *payload, size_t payload_len);
static void sunpci_dispatch_clipboard(struct sunpci_device *dev,
                                      u16 command, u32 sequence,
                                      void *payload, size_t payload_len);

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

    /* Wake up any threads waiting for responses */
    wake_up_interruptible(&dev->rsp_wait);
}

/*
 * Send a response back to the guest
 *
 * @dev: Device context
 * @sequence: Sequence number from the original request
 * @status: Response status code (SUNPCI_RSP_*)
 * @payload: Response payload data
 * @payload_len: Length of payload
 *
 * Returns: 0 on success, negative errno on failure
 */
int sunpci_ipc_send_response(struct sunpci_device *dev,
                             u32 sequence, u16 status,
                             const void *payload, size_t payload_len)
{
    struct sunpci_rsp_header hdr;
    size_t total_len;
    int ret;

    if (!dev || !dev->rsp_ring.base)
        return -EINVAL;

    if (payload_len > SUNPCI_MAX_PAYLOAD)
        return -EINVAL;

    total_len = sizeof(hdr) + payload_len;

    /* Build response header */
    hdr.magic = cpu_to_le32(SUNPCI_MSG_MAGIC);
    hdr.status = cpu_to_le16(status);
    hdr.reserved = 0;
    hdr.sequence = cpu_to_le32(sequence);
    hdr.payload_len = cpu_to_le32(payload_len);

    /* Check ring has space */
    if (sunpci_ring_space(&dev->rsp_ring) < total_len) {
        dev_warn(&dev->pdev->dev, "rsp ring full\n");
        return -ENOSPC;
    }

    /* Write header */
    ret = sunpci_ring_write(&dev->rsp_ring, &hdr, sizeof(hdr));
    if (ret < 0)
        return ret;

    /* Write payload if present */
    if (payload && payload_len > 0) {
        ret = sunpci_ring_write(&dev->rsp_ring, payload, payload_len);
        if (ret < 0)
            return ret;
    }

    /* Sync tail pointer to hardware */
    sunpci_ring_sync_tail(&dev->rsp_ring, dev);

    /* Ring doorbell to notify guest that response is ready */
    iowrite32(SUNPCI_DOORBELL_RSP_READY,
              dev->mmio_base + I21554_PRI_DOORBELL);

    return 0;
}

/*
 * Process pending requests from guest
 *
 * Called from workqueue when we receive CMD_READY doorbell.
 * This runs in process context so we can do blocking I/O.
 */
void sunpci_ipc_process_requests(struct work_struct *work)
{
    struct sunpci_device *dev = container_of(work, struct sunpci_device,
                                             request_work);
    struct sunpci_msg_header hdr;
    u8 *payload_buf = NULL;
    size_t payload_len;
    u32 sequence;
    u16 dispatcher, command;
    int ret;

    /* Allocate payload buffer */
    payload_buf = kmalloc(SUNPCI_MAX_PAYLOAD, GFP_KERNEL);
    if (!payload_buf) {
        dev_err(&dev->pdev->dev, "failed to allocate request buffer\n");
        return;
    }

    /* Process all pending requests */
    while (1) {
        /* Sync head pointer from hardware */
        sunpci_ring_sync_head(&dev->rsp_ring, dev);

        /* Check if we have a complete header */
        if (sunpci_ring_used(&dev->rsp_ring) < sizeof(hdr))
            break;

        /* Peek at header without consuming */
        ret = sunpci_ring_peek(&dev->rsp_ring, &hdr, sizeof(hdr));
        if (ret < 0)
            break;

        /* Validate magic */
        if (le32_to_cpu(hdr.magic) != SUNPCI_MSG_MAGIC) {
            dev_err(&dev->pdev->dev, "bad request magic: 0x%08x\n",
                    le32_to_cpu(hdr.magic));
            /* Try to recover by skipping a byte */
            sunpci_ring_skip(&dev->rsp_ring, 1);
            continue;
        }

        /* Have full message? */
        payload_len = le32_to_cpu(hdr.payload_len);
        if (sunpci_ring_used(&dev->rsp_ring) < sizeof(hdr) + payload_len)
            break;  /* Wait for more data */

        /* Extract header fields */
        sequence = le32_to_cpu(hdr.sequence);
        dispatcher = le16_to_cpu(hdr.dispatcher);
        command = le16_to_cpu(hdr.command);

        /* Consume header */
        sunpci_ring_skip(&dev->rsp_ring, sizeof(hdr));

        /* Read payload */
        if (payload_len > 0) {
            if (payload_len > SUNPCI_MAX_PAYLOAD) {
                dev_err(&dev->pdev->dev, "payload too large: %zu\n",
                        payload_len);
                sunpci_ring_skip(&dev->rsp_ring, payload_len);
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_ERROR, NULL, 0);
                continue;
            }
            ret = sunpci_ring_read(&dev->rsp_ring, payload_buf, payload_len);
            if (ret < 0) {
                dev_err(&dev->pdev->dev, "failed to read payload\n");
                continue;
            }
        }

        /* Dispatch to appropriate handler */
        switch (dispatcher) {
        case SUNPCI_DISP_CORE:
            /* Handle channel commands */
            if (command >= CORE_CMD_CHANNEL_CREATE && 
                command <= CORE_CMD_CHANNEL_UNBIND) {
                sunpci_dispatch_channel(dev, command, sequence,
                                       payload_buf, payload_len);
            } else {
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_INVALID_CMD, NULL, 0);
            }
            break;

        case SUNPCI_DISP_STORAGE:
            sunpci_dispatch_storage(dev, command, sequence,
                                   payload_buf, payload_len);
            break;

        case SUNPCI_DISP_NETWORK:
            sunpci_dispatch_network(dev, command, sequence,
                                   payload_buf, payload_len);
            break;

        case SUNPCI_DISP_FSD:
            sunpci_dispatch_fsd(dev, command, sequence,
                               payload_buf, payload_len);
            break;

        case SUNPCI_DISP_CLIP:
            sunpci_dispatch_clipboard(dev, command, sequence,
                                     payload_buf, payload_len);
            break;

        default:
            dev_dbg(&dev->pdev->dev, "unknown dispatcher: %d\n", dispatcher);
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_INVALID_DISP, NULL, 0);
            break;
        }
    }

    kfree(payload_buf);
}

/*
 * Dispatch storage request
 */
static void sunpci_dispatch_storage(struct sunpci_device *dev,
                                    u16 command, u32 sequence,
                                    void *payload, size_t payload_len)
{
    struct sunpci_storage_req *req;
    struct sunpci_storage_rsp rsp;
    struct sunpci_scsi_req *scsi_req;
    struct sunpci_scsi_rsp scsi_rsp;
    u8 *data_buf;
    size_t data_len;
    int ret;

    /* Response buffer - enough for sector data */
    data_buf = kmalloc(SCSI_DATA_MAX_LEN, GFP_KERNEL);
    if (!data_buf) {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
        return;
    }
    data_len = SCSI_DATA_MAX_LEN;

    if (command == STORAGE_CMD_SCSI) {
        /* SCSI CDB pass-through */
        if (payload_len < sizeof(*scsi_req)) {
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_INVALID_CMD, NULL, 0);
            goto out;
        }
        scsi_req = payload;
        
        ret = sunpci_storage_scsi_command(dev, scsi_req, &scsi_rsp,
                                          data_buf, data_len);
        if (ret < 0) {
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_ERROR, NULL, 0);
            goto out;
        }

        /* Send SCSI response with any data */
        data_len = le32_to_cpu(scsi_rsp.data_len);
        if (data_len > 0) {
            /* Combine response header and data */
            size_t total = sizeof(scsi_rsp) + data_len;
            u8 *combined = kmalloc(total, GFP_KERNEL);
            if (combined) {
                memcpy(combined, &scsi_rsp, sizeof(scsi_rsp));
                memcpy(combined + sizeof(scsi_rsp), data_buf, data_len);
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_SUCCESS, combined, total);
                kfree(combined);
            } else {
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_ERROR, NULL, 0);
            }
        } else {
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_SUCCESS,
                                    &scsi_rsp, sizeof(scsi_rsp));
        }
    } else {
        /* Standard INT 13h-style request */
        if (payload_len < sizeof(*req)) {
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_INVALID_CMD, NULL, 0);
            goto out;
        }
        req = payload;

        ret = sunpci_storage_handle_request(dev, req, &rsp, data_buf, data_len);
        if (ret < 0) {
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_ERROR, NULL, 0);
            goto out;
        }

        /* For reads, send response with data; for others, just response */
        if (le32_to_cpu(req->command) == STORAGE_CMD_READ) {
            u32 xfer_count = le32_to_cpu(rsp.count);
            size_t total = sizeof(rsp) + (xfer_count * 512); /* Assume 512 byte sectors */
            u8 *combined = kmalloc(total, GFP_KERNEL);
            if (combined) {
                memcpy(combined, &rsp, sizeof(rsp));
                memcpy(combined + sizeof(rsp), data_buf, xfer_count * 512);
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_SUCCESS, combined, total);
                kfree(combined);
            } else {
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_ERROR, NULL, 0);
            }
        } else if (le32_to_cpu(req->command) == STORAGE_CMD_GET_PARAMS) {
            /* Parameters response includes the params struct */
            size_t total = sizeof(rsp) + sizeof(struct sunpci_storage_params);
            u8 *combined = kmalloc(total, GFP_KERNEL);
            if (combined) {
                memcpy(combined, &rsp, sizeof(rsp));
                memcpy(combined + sizeof(rsp), data_buf,
                       sizeof(struct sunpci_storage_params));
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_SUCCESS, combined, total);
                kfree(combined);
            } else {
                sunpci_ipc_send_response(dev, sequence,
                                        SUNPCI_RSP_ERROR, NULL, 0);
            }
        } else {
            /* Simple response without data */
            sunpci_ipc_send_response(dev, sequence,
                                    SUNPCI_RSP_SUCCESS, &rsp, sizeof(rsp));
        }
    }

out:
    kfree(data_buf);
}

/*
 * Dispatch network request
 */
static void sunpci_dispatch_network(struct sunpci_device *dev,
                                    u16 command, u32 sequence,
                                    void *payload, size_t payload_len)
{
    struct sunpci_net_req *req = payload;
    struct sunpci_net_rsp rsp;
    u8 *data_buf;
    size_t data_len = 2048;  /* Ethernet MTU + headers */
    int ret;

    if (payload_len < sizeof(*req)) {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD, NULL, 0);
        return;
    }

    data_buf = kmalloc(data_len, GFP_KERNEL);
    if (!data_buf) {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
        return;
    }

    ret = sunpci_net_handle_request(dev, req, &rsp, data_buf, data_len);
    if (ret < 0) {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
    } else {
        /* Send response with any received packet data */
        size_t total = sizeof(rsp) + le32_to_cpu(rsp.length);
        u8 *combined = kmalloc(total, GFP_KERNEL);
        if (combined) {
            memcpy(combined, &rsp, sizeof(rsp));
            if (le32_to_cpu(rsp.length) > 0)
                memcpy(combined + sizeof(rsp), data_buf, le32_to_cpu(rsp.length));
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS, combined, total);
            kfree(combined);
        } else {
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
        }
    }
    kfree(data_buf);
}

/*
 * Dispatch FSD (filesystem redirection) request
 */
static void sunpci_dispatch_fsd(struct sunpci_device *dev,
                                u16 command, u32 sequence,
                                void *payload, size_t payload_len)
{
    u8 *rsp_buf;
    size_t rsp_len = 64 * 1024;  /* 64KB for directory listings */
    int ret;

    rsp_buf = kmalloc(rsp_len, GFP_KERNEL);
    if (!rsp_buf) {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
        return;
    }

    ret = sunpci_fsd_handle_message(dev, command, payload, payload_len,
                                    rsp_buf, &rsp_len);
    if (ret < 0) {
        if (ret == -ENOSYS)
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD, NULL, 0);
        else
            sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR, NULL, 0);
    } else {
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS, rsp_buf, rsp_len);
    }
    kfree(rsp_buf);
}

/*
 * Dispatch clipboard request
 */
static void sunpci_dispatch_clipboard(struct sunpci_device *dev,
                                      u16 command, u32 sequence,
                                      void *payload, size_t payload_len)
{
    switch (command) {
    case CLIP_CMD_NOTIFY:
        /* Guest clipboard changed - store it */
        sunpci_clip_handle_notify(dev, payload, payload_len);
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS, NULL, 0);
        break;

    case CLIP_CMD_DATA:
        /* Guest sending clipboard data we requested */
        sunpci_clip_handle_notify(dev, payload, payload_len);
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS, NULL, 0);
        break;

    case CLIP_CMD_GET:
        /* Guest requesting host clipboard - send it */
        {
            struct sunpci_clipboard *clip;
            clip = kmalloc(sizeof(*clip), GFP_KERNEL);
            if (!clip) {
                sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR,
                                        NULL, 0);
                break;
            }
            int ret = sunpci_clip_get(dev, clip);
            if (ret == 0 && clip->length > 0) {
                struct sunpci_clip_data *msg;
                size_t msg_len = sizeof(*msg) + clip->length;
                msg = kmalloc(msg_len, GFP_KERNEL);
                if (msg) {
                    msg->format = cpu_to_le32(clip->format);
                    msg->length = cpu_to_le32(clip->length);
                    memcpy(msg + 1, clip->data, clip->length);
                    sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS,
                                            msg, msg_len);
                    kfree(msg);
                } else {
                    sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_ERROR,
                                            NULL, 0);
                }
            } else {
                sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_SUCCESS,
                                        NULL, 0);
            }
            kfree(clip);
        }
        break;

    default:
        sunpci_ipc_send_response(dev, sequence, SUNPCI_RSP_INVALID_CMD, NULL, 0);
        break;
    }
}
