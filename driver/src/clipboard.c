/*
 * SunPCI driver - Clipboard transfer
 *
 * Handles clipboard synchronization between host and guest.
 * The guest runs Windows which uses UTF-16LE for Unicode text.
 */

#include <linux/slab.h>
#include <linux/uaccess.h>

#include "sunpci.h"
#include "ipc.h"

/**
 * sunpci_clip_set - Send clipboard data to guest
 * @dev: Device
 * @clip: Clipboard data from userspace
 *
 * Sends the host clipboard content to the Windows guest.
 * Text is expected to be in the format specified by clip->format.
 */
int sunpci_clip_set(struct sunpci_device *dev,
                    const struct sunpci_clipboard *clip)
{
    struct sunpci_clip_data *msg;
    size_t msg_len;
    int ret;

    if (!dev || !clip)
        return -EINVAL;

    if (dev->state != SUNPCI_STATE_RUNNING)
        return -ENODEV;

    if (clip->length == 0)
        return 0;  /* Nothing to send */

    if (clip->length > SUNPCI_MAX_CLIPBOARD)
        return -EINVAL;

    /* Allocate message with data appended */
    msg_len = sizeof(*msg) + clip->length;
    msg = kmalloc(msg_len, GFP_KERNEL);
    if (!msg)
        return -ENOMEM;

    /* Build clipboard message */
    msg->format = cpu_to_le32(clip->format == SUNPCI_CLIPBOARD_UNICODE ?
                              CLIP_FORMAT_UNICODE : CLIP_FORMAT_TEXT);
    msg->length = cpu_to_le32(clip->length);
    memcpy(msg + 1, clip->data, clip->length);

    /* Send to guest */
    ret = sunpci_ipc_send_cmd(dev, SUNPCI_DISP_CLIP, CLIP_CMD_SET,
                              msg, msg_len, NULL);

    kfree(msg);

    if (ret < 0) {
        dev_dbg(&dev->pdev->dev, "clip_set failed: %d\n", ret);
        return ret;
    }

    dev_dbg(&dev->pdev->dev, "clipboard sent: %u bytes, format %u\n",
            clip->length, clip->format);

    return 0;
}

/**
 * sunpci_clip_get - Request clipboard data from guest
 * @dev: Device
 * @clip: Buffer for clipboard data
 *
 * Requests the current clipboard content from the Windows guest.
 * This is a synchronous operation - waits for the response.
 */
int sunpci_clip_get(struct sunpci_device *dev,
                    struct sunpci_clipboard *clip)
{
    struct sunpci_clip_data *rsp;
    size_t rsp_len;
    size_t actual_len;
    u32 format;
    int ret;

    if (!dev || !clip)
        return -EINVAL;

    if (dev->state != SUNPCI_STATE_RUNNING)
        return -ENODEV;

    /* Allocate response buffer */
    rsp_len = sizeof(*rsp) + SUNPCI_CLIP_MAX_SIZE;
    rsp = kmalloc(rsp_len, GFP_KERNEL);
    if (!rsp)
        return -ENOMEM;

    /* Request clipboard from guest */
    ret = sunpci_ipc_transact(dev, SUNPCI_DISP_CLIP, CLIP_CMD_GET,
                              NULL, 0,  /* No request payload */
                              rsp, rsp_len, &actual_len,
                              SUNPCI_CMD_TIMEOUT);

    if (ret < 0) {
        dev_dbg(&dev->pdev->dev, "clip_get failed: %d\n", ret);
        kfree(rsp);
        return ret;
    }

    /* Parse response */
    if (actual_len < sizeof(*rsp)) {
        dev_warn(&dev->pdev->dev, "clip_get: short response\n");
        kfree(rsp);
        return -EIO;
    }

    format = le32_to_cpu(rsp->format);
    clip->length = le32_to_cpu(rsp->length);
    
    /* Map Windows format to our format */
    if (format == CLIP_FORMAT_UNICODE)
        clip->format = SUNPCI_CLIPBOARD_UNICODE;
    else
        clip->format = SUNPCI_CLIPBOARD_TEXT;

    /* Copy data, truncating if necessary */
    if (clip->length > SUNPCI_MAX_CLIPBOARD)
        clip->length = SUNPCI_MAX_CLIPBOARD;

    if (clip->length > 0 && actual_len >= sizeof(*rsp) + clip->length)
        memcpy(clip->data, rsp + 1, clip->length);

    kfree(rsp);

    dev_dbg(&dev->pdev->dev, "clipboard received: %u bytes, format %u\n",
            clip->length, clip->format);

    return 0;
}

/**
 * sunpci_clip_handle_notify - Handle clipboard change notification from guest
 * @dev: Device
 * @data: Notification data
 * @len: Data length
 *
 * Called when the guest notifies us that its clipboard has changed.
 * We store the data locally and userspace can retrieve it via ioctl.
 */
void sunpci_clip_handle_notify(struct sunpci_device *dev,
                               const void *data, size_t len)
{
    const struct sunpci_clip_data *clip_data = data;
    u32 format, length;

    if (!dev || !data || len < sizeof(*clip_data))
        return;

    format = le32_to_cpu(clip_data->format);
    length = le32_to_cpu(clip_data->length);

    if (length > SUNPCI_MAX_CLIPBOARD)
        length = SUNPCI_MAX_CLIPBOARD;

    if (len < sizeof(*clip_data) + length)
        return;  /* Incomplete data */

    mutex_lock(&dev->mutex);

    /* Store clipboard data */
    dev->clipboard.length = length;
    if (format == CLIP_FORMAT_UNICODE)
        dev->clipboard.format = SUNPCI_CLIPBOARD_UNICODE;
    else
        dev->clipboard.format = SUNPCI_CLIPBOARD_TEXT;

    if (length > 0)
        memcpy(dev->clipboard.data, clip_data + 1, length);

    mutex_unlock(&dev->mutex);

    dev_dbg(&dev->pdev->dev, "guest clipboard updated: %u bytes\n", length);

    /* TODO: Signal userspace via poll/select that clipboard changed */
}
