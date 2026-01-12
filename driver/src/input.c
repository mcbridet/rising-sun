// SPDX-License-Identifier: GPL-2.0
/*
 * SunPCI driver - Input event injection
 *
 * Injects keyboard and mouse events into the guest.
 */

#include <linux/input.h>
#include <linux/uaccess.h>

#include "sunpci.h"
#include "ipc.h"

/**
 * sunpci_inject_key - Inject a keyboard event
 * @dev: Device
 * @event: Key event from userspace
 *
 * Sends a keyboard scancode to the guest x86 system.
 */
int sunpci_inject_key(struct sunpci_device *dev,
                      const struct sunpci_key_event *event)
{
    struct sunpci_input_keyboard msg;
    int ret;

    if (!dev || !event)
        return -EINVAL;

    if (dev->state != SUNPCI_STATE_RUNNING)
        return -ENODEV;

    /* Build keyboard message */
    msg.scancode = cpu_to_le16(event->scancode & 0xFFFF);
    msg.flags = 0;
    
    if (event->flags & SUNPCI_KEY_PRESSED)
        msg.flags |= cpu_to_le16(INPUT_KEY_PRESSED);
    else
        msg.flags |= cpu_to_le16(INPUT_KEY_RELEASED);
    if (event->flags & SUNPCI_KEY_EXTENDED)
        msg.flags |= cpu_to_le16(INPUT_KEY_EXTENDED);

    /* Send to guest via IPC */
    ret = sunpci_ipc_send_cmd(dev, SUNPCI_DISP_INPUT, INPUT_CMD_KEYBOARD,
                              &msg, sizeof(msg), NULL);
    if (ret < 0) {
        dev_dbg(&dev->pdev->dev, "inject_key failed: %d\n", ret);
        return ret;
    }

    return 0;
}

/**
 * sunpci_inject_mouse - Inject a mouse event
 * @dev: Device
 * @event: Mouse event from userspace
 *
 * Sends mouse movement/button state to the guest.
 */
int sunpci_inject_mouse(struct sunpci_device *dev,
                        const struct sunpci_mouse_event *event)
{
    struct sunpci_input_mouse msg;
    int ret;

    if (!dev || !event)
        return -EINVAL;

    if (dev->state != SUNPCI_STATE_RUNNING)
        return -ENODEV;

    /* Build mouse message - convert relative to what the IPC expects */
    msg.x = cpu_to_le32(event->dx);
    msg.y = cpu_to_le32(event->dy);
    msg.wheel = cpu_to_le32(event->dz);
    
    /* Map userspace button flags to IPC button flags */
    msg.buttons = 0;
    if (event->buttons & SUNPCI_MOUSE_LEFT)
        msg.buttons |= cpu_to_le32(INPUT_MOUSE_LEFT);
    if (event->buttons & SUNPCI_MOUSE_RIGHT)
        msg.buttons |= cpu_to_le32(INPUT_MOUSE_RIGHT);
    if (event->buttons & SUNPCI_MOUSE_MIDDLE)
        msg.buttons |= cpu_to_le32(INPUT_MOUSE_MIDDLE);

    ret = sunpci_ipc_send_cmd(dev, SUNPCI_DISP_INPUT, INPUT_CMD_MOUSE_MOVE,
                              &msg, sizeof(msg), NULL);
    if (ret < 0) {
        dev_dbg(&dev->pdev->dev, "inject_mouse failed: %d\n", ret);
        return ret;
    }

    return 0;
}
