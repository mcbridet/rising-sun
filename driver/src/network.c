/*
 * SunPCi driver - Network subsystem
 *
 * Provides Ethernet connectivity to the guest via TAP device bridging.
 * The guest sees a virtual NIC that communicates through the host's
 * network stack using a TAP interface.
 *
 * Supports:
 *   - TAP device creation and management
 *   - Packet send/receive via ring buffers
 *   - Multicast filtering
 *   - Link state notifications
 *   - Guest IRQ generation on packet receive
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_ether.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>

#include "sunpci.h"
#include "ipc.h"

/* Maximum Ethernet frame size */
#define ETH_FRAME_MAX       1514
#define ETH_FRAME_MIN       60

/* Receive queue depth */
#define NET_RX_QUEUE_SIZE   64

/* TAP device path */
#define TUN_DEV_PATH        "/dev/net/tun"

/* Supported guest IRQ lines for network */
#define NET_IRQ_9           9
#define NET_IRQ_10          10
#define NET_IRQ_11          11
#define NET_IRQ_15          15

/*
 * Network device state
 */
struct sunpci_net_dev {
    /* TAP device */
    struct file *tap_file;
    char tap_name[IFNAMSIZ];
    
    /* Configuration */
    u8 mac_addr[ETH_ALEN];
    u8 irq_line;
    bool promiscuous;
    bool enabled;
    
    /* Multicast filter */
    u8 mcast_list[32][ETH_ALEN];  /* Up to 32 multicast addresses */
    size_t mcast_count;
    bool allmulti;
    
    /* Statistics */
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_bytes;
    u64 tx_bytes;
    u64 rx_dropped;
    u64 tx_dropped;
    
    /* Receive queue */
    struct {
        u8 data[ETH_FRAME_MAX];
        size_t len;
    } rx_queue[NET_RX_QUEUE_SIZE];
    size_t rx_head;
    size_t rx_tail;
    size_t rx_count;
    spinlock_t rx_lock;
    
    /* Receive thread */
    struct task_struct *rx_thread;
    bool rx_running;
    
    /* Parent device */
    struct sunpci_device *dev;
};

/*
 * Generate a locally administered MAC address
 * Uses device minor + random bytes for uniqueness
 */
static void generate_mac_address(struct sunpci_device *dev, u8 *mac)
{
    /* Get random bytes */
    get_random_bytes(mac, ETH_ALEN);
    
    /* Make it locally administered and unicast */
    mac[0] = (mac[0] & 0xFE) | 0x02;  /* Clear multicast, set local */
    
    /* Include minor number for device uniqueness */
    mac[5] = (mac[5] & 0xF0) | (dev->minor & 0x0F);
}

/*
 * Open TAP device
 */
static int net_open_tap(struct sunpci_net_dev *ndev, const char *name)
{
    struct file *file;
    struct ifreq ifr;
    int ret;
    
    file = filp_open(TUN_DEV_PATH, O_RDWR, 0);
    if (IS_ERR(file)) {
        pr_err("sunpci: failed to open %s: %ld\n", TUN_DEV_PATH, PTR_ERR(file));
        return PTR_ERR(file);
    }
    
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;  /* TAP device, no packet info */
    
    if (name && name[0]) {
        strscpy(ifr.ifr_name, name, IFNAMSIZ);
    } else {
        strscpy(ifr.ifr_name, "sunpci%d", IFNAMSIZ);
    }
    
    /* Configure TAP device via ioctl - use file's unlocked_ioctl */
    if (!file->f_op || !file->f_op->unlocked_ioctl) {
        pr_err("sunpci: TUN device has no ioctl support\n");
        filp_close(file, NULL);
        return -ENOTTY;
    }
    
    ret = file->f_op->unlocked_ioctl(file, TUNSETIFF, (unsigned long)&ifr);
    if (ret < 0) {
        pr_err("sunpci: TUNSETIFF failed: %d\n", ret);
        filp_close(file, NULL);
        return ret;
    }
    
    ndev->tap_file = file;
    strscpy(ndev->tap_name, ifr.ifr_name, IFNAMSIZ);
    
    pr_info("sunpci: created TAP device %s\n", ndev->tap_name);
    return 0;
}

/*
 * Close TAP device
 */
static void net_close_tap(struct sunpci_net_dev *ndev)
{
    if (ndev->tap_file) {
        filp_close(ndev->tap_file, NULL);
        ndev->tap_file = NULL;
        pr_info("sunpci: closed TAP device %s\n", ndev->tap_name);
    }
}

/*
 * Check if MAC address passes the filter
 */
static bool net_mac_filter(struct sunpci_net_dev *ndev, const u8 *dest)
{
    size_t i;
    
    /* Broadcast always passes */
    if (is_broadcast_ether_addr(dest))
        return true;
    
    /* Our unicast MAC always passes */
    if (ether_addr_equal(dest, ndev->mac_addr))
        return true;
    
    /* Promiscuous mode accepts all */
    if (ndev->promiscuous)
        return true;
    
    /* Multicast handling */
    if (is_multicast_ether_addr(dest)) {
        if (ndev->allmulti)
            return true;
        
        /* Check multicast list */
        for (i = 0; i < ndev->mcast_count; i++) {
            if (ether_addr_equal(dest, ndev->mcast_list[i]))
                return true;
        }
        return false;
    }
    
    /* Unknown unicast */
    return false;
}

/*
 * Enqueue received packet
 */
static int net_rx_enqueue(struct sunpci_net_dev *ndev, const u8 *data, size_t len)
{
    unsigned long flags;
    
    if (len > ETH_FRAME_MAX)
        len = ETH_FRAME_MAX;
    
    spin_lock_irqsave(&ndev->rx_lock, flags);
    
    if (ndev->rx_count >= NET_RX_QUEUE_SIZE) {
        ndev->rx_dropped++;
        spin_unlock_irqrestore(&ndev->rx_lock, flags);
        return -ENOBUFS;
    }
    
    memcpy(ndev->rx_queue[ndev->rx_tail].data, data, len);
    ndev->rx_queue[ndev->rx_tail].len = len;
    ndev->rx_tail = (ndev->rx_tail + 1) % NET_RX_QUEUE_SIZE;
    ndev->rx_count++;
    
    spin_unlock_irqrestore(&ndev->rx_lock, flags);
    return 0;
}

/*
 * Dequeue received packet
 */
static int net_rx_dequeue(struct sunpci_net_dev *ndev, u8 *data, size_t *len)
{
    unsigned long flags;
    
    spin_lock_irqsave(&ndev->rx_lock, flags);
    
    if (ndev->rx_count == 0) {
        spin_unlock_irqrestore(&ndev->rx_lock, flags);
        return -ENODATA;
    }
    
    *len = ndev->rx_queue[ndev->rx_head].len;
    memcpy(data, ndev->rx_queue[ndev->rx_head].data, *len);
    ndev->rx_head = (ndev->rx_head + 1) % NET_RX_QUEUE_SIZE;
    ndev->rx_count--;
    
    spin_unlock_irqrestore(&ndev->rx_lock, flags);
    return 0;
}

/*
 * Receive thread - reads packets from TAP and queues them
 */
static int net_rx_thread(void *data)
{
    struct sunpci_net_dev *ndev = data;
    u8 buf[ETH_FRAME_MAX];
    loff_t pos = 0;
    ssize_t ret;
    
    while (!kthread_should_stop() && ndev->rx_running) {
        /* Read packet from TAP */
        ret = kernel_read(ndev->tap_file, buf, sizeof(buf), &pos);
        if (ret < 0) {
            if (ret == -EAGAIN || ret == -EINTR)
                continue;
            pr_err("sunpci: TAP read error: %zd\n", ret);
            break;
        }
        
        if (ret < ETH_HLEN)
            continue;  /* Too short */
        
        /* Apply MAC filter */
        if (!net_mac_filter(ndev, buf))
            continue;
        
        /* Enqueue packet */
        if (net_rx_enqueue(ndev, buf, ret) == 0) {
            ndev->rx_packets++;
            ndev->rx_bytes += ret;
            
            /* Notify guest that data is ready */
            sunpci_net_notify_rx(ndev->dev);
        }
    }
    
    return 0;
}

/*
 * Send packet to TAP device
 */
static int net_send_packet(struct sunpci_net_dev *ndev, const u8 *data, size_t len)
{
    loff_t pos = 0;
    ssize_t ret;
    
    if (!ndev->tap_file || !ndev->enabled)
        return -ENODEV;
    
    if (len < ETH_FRAME_MIN || len > ETH_FRAME_MAX)
        return -EINVAL;
    
    ret = kernel_write(ndev->tap_file, data, len, &pos);
    if (ret < 0) {
        ndev->tx_dropped++;
        return ret;
    }
    
    ndev->tx_packets++;
    ndev->tx_bytes += len;
    return 0;
}

/*
 * Notify guest of pending receive data
 */
void sunpci_net_notify_rx(struct sunpci_device *dev)
{
    struct sunpci_net_dev *ndev = dev->net_dev;
    struct {
        __le32 irq;
    } msg;
    
    if (!ndev || !ndev->enabled)
        return;
    
    msg.irq = cpu_to_le32(ndev->irq_line);
    
    sunpci_ipc_send_cmd(dev, SUNPCI_DISP_NETWORK, NET_CMD_DATA_READY,
                        &msg, sizeof(msg), NULL);
}

/*
 * Handle NDIS protocol request from guest
 */
int sunpci_net_handle_request(struct sunpci_device *dev,
                              const struct sunpci_net_req *req,
                              struct sunpci_net_rsp *rsp,
                              void *data_buf, size_t data_len)
{
    struct sunpci_net_dev *ndev = dev->net_dev;
    int ret;
    
    if (!ndev) {
        rsp->status = cpu_to_le32(NET_STATUS_NO_DEVICE);
        return 0;
    }
    
    switch (le32_to_cpu(req->command)) {
    case NET_CMD_INIT:
        /* Initialize adapter */
        ndev->irq_line = le32_to_cpu(req->param1) & 0xFF;
        
        /* Validate IRQ */
        if (ndev->irq_line != NET_IRQ_9 &&
            ndev->irq_line != NET_IRQ_10 &&
            ndev->irq_line != NET_IRQ_11 &&
            ndev->irq_line != NET_IRQ_15) {
            pr_warn("sunpci: unsupported network IRQ %d, using 10\n",
                    ndev->irq_line);
            ndev->irq_line = NET_IRQ_10;
        }
        
        /* Return MAC address */
        memcpy(data_buf, ndev->mac_addr, ETH_ALEN);
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        rsp->length = cpu_to_le32(ETH_ALEN);
        break;
        
    case NET_CMD_OPEN:
        /* Open adapter - start receive thread */
        if (!ndev->rx_running && ndev->tap_file) {
            ndev->rx_running = true;
            ndev->rx_thread = kthread_run(net_rx_thread, ndev,
                                          "sunpci-net%d", dev->minor);
            if (IS_ERR(ndev->rx_thread)) {
                ndev->rx_running = false;
                rsp->status = cpu_to_le32(NET_STATUS_ERROR);
                break;
            }
        }
        ndev->enabled = true;
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        break;
        
    case NET_CMD_CLOSE:
        /* Close adapter - stop receive thread */
        ndev->enabled = false;
        if (ndev->rx_running) {
            ndev->rx_running = false;
            if (ndev->rx_thread) {
                kthread_stop(ndev->rx_thread);
                ndev->rx_thread = NULL;
            }
        }
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        break;
        
    case NET_CMD_SEND:
        /* Send packet */
        if (data_len < ETH_HLEN) {
            rsp->status = cpu_to_le32(NET_STATUS_BAD_PACKET);
            break;
        }
        ret = net_send_packet(ndev, data_buf, data_len);
        rsp->status = cpu_to_le32(ret == 0 ? NET_STATUS_OK : NET_STATUS_ERROR);
        break;
        
    case NET_CMD_RECV:
        /* Receive packet */
        {
            size_t pkt_len;
            ret = net_rx_dequeue(ndev, data_buf, &pkt_len);
            if (ret == 0) {
                rsp->status = cpu_to_le32(NET_STATUS_OK);
                rsp->length = cpu_to_le32(pkt_len);
            } else {
                rsp->status = cpu_to_le32(NET_STATUS_NO_DATA);
                rsp->length = 0;
            }
        }
        break;
        
    case NET_CMD_SET_MCAST:
        /* Set multicast filter list */
        {
            size_t count = le32_to_cpu(req->param1);
            size_t i;
            
            if (count > 32)
                count = 32;
            
            ndev->mcast_count = count;
            for (i = 0; i < count; i++) {
                memcpy(ndev->mcast_list[i],
                       (u8 *)data_buf + i * ETH_ALEN,
                       ETH_ALEN);
            }
            rsp->status = cpu_to_le32(NET_STATUS_OK);
        }
        break;
        
    case NET_CMD_SET_PROMISC:
        /* Set promiscuous mode */
        ndev->promiscuous = (le32_to_cpu(req->param1) != 0);
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        break;
        
    case NET_CMD_SET_ALLMULTI:
        /* Set all-multicast mode */
        ndev->allmulti = (le32_to_cpu(req->param1) != 0);
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        break;
        
    case NET_CMD_GET_STATS:
        /* Return statistics */
        {
            struct sunpci_net_stats *stats = data_buf;
            stats->rx_packets = cpu_to_le64(ndev->rx_packets);
            stats->tx_packets = cpu_to_le64(ndev->tx_packets);
            stats->rx_bytes = cpu_to_le64(ndev->rx_bytes);
            stats->tx_bytes = cpu_to_le64(ndev->tx_bytes);
            stats->rx_dropped = cpu_to_le64(ndev->rx_dropped);
            stats->tx_dropped = cpu_to_le64(ndev->tx_dropped);
            rsp->status = cpu_to_le32(NET_STATUS_OK);
            rsp->length = cpu_to_le32(sizeof(*stats));
        }
        break;
        
    case NET_CMD_INT_REL:
        /* Interrupt release - guest acknowledged IRQ */
        rsp->status = cpu_to_le32(NET_STATUS_OK);
        break;
        
    default:
        rsp->status = cpu_to_le32(NET_STATUS_BAD_CMD);
        break;
    }
    
    return 0;
}

/*
 * Initialize network subsystem
 */
int sunpci_net_init(struct sunpci_device *dev)
{
    struct sunpci_net_dev *ndev;
    int ret;
    
    ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
    if (!ndev)
        return -ENOMEM;
    
    ndev->dev = dev;
    spin_lock_init(&ndev->rx_lock);
    
    /* Generate MAC address */
    if (is_zero_ether_addr(dev->network.mac_address)) {
        generate_mac_address(dev, ndev->mac_addr);
    } else {
        memcpy(ndev->mac_addr, dev->network.mac_address, ETH_ALEN);
    }
    
    /* Open TAP device if networking is enabled */
    if (dev->network.flags & SUNPCI_NET_ENABLED) {
        ret = net_open_tap(ndev, dev->network.interface);
        if (ret < 0) {
            kfree(ndev);
            return ret;
        }
    }
    
    /* Set default IRQ */
    ndev->irq_line = NET_IRQ_10;
    
    dev->net_dev = ndev;
    
    pr_info("sunpci: network initialized, MAC=%pM\n", ndev->mac_addr);
    return 0;
}

/*
 * Configure network interface
 */
int sunpci_net_configure(struct sunpci_device *dev,
                         const struct sunpci_network_config *config)
{
    struct sunpci_net_dev *ndev = dev->net_dev;
    int ret = 0;
    
    if (!ndev)
        return -ENODEV;
    
    /* Update MAC if provided */
    if (!is_zero_ether_addr(config->mac_address))
        memcpy(ndev->mac_addr, config->mac_address, ETH_ALEN);
    
    /* Handle enable/disable */
    if ((config->flags & SUNPCI_NET_ENABLED) && !ndev->tap_file) {
        /* Enable networking - open TAP */
        ret = net_open_tap(ndev, config->interface);
    } else if (!(config->flags & SUNPCI_NET_ENABLED) && ndev->tap_file) {
        /* Disable networking - close TAP */
        if (ndev->rx_running) {
            ndev->rx_running = false;
            if (ndev->rx_thread) {
                kthread_stop(ndev->rx_thread);
                ndev->rx_thread = NULL;
            }
        }
        net_close_tap(ndev);
        ndev->enabled = false;
    }
    
    /* Update promiscuous mode */
    ndev->promiscuous = (config->flags & SUNPCI_NET_PROMISCUOUS) != 0;
    
    /* Store interface name */
    if (config->interface[0])
        strscpy(ndev->tap_name, config->interface, IFNAMSIZ);
    
    return ret;
}

/*
 * Get network status
 */
int sunpci_net_get_status(struct sunpci_device *dev,
                          struct sunpci_network_status *status)
{
    struct sunpci_net_dev *ndev = dev->net_dev;
    
    if (!ndev) {
        memset(status, 0, sizeof(*status));
        return 0;
    }
    
    status->flags = ndev->enabled ? SUNPCI_NET_ENABLED : 0;
    if (ndev->promiscuous)
        status->flags |= SUNPCI_NET_PROMISCUOUS;
    
    status->rx_packets = ndev->rx_packets;
    status->tx_packets = ndev->tx_packets;
    status->rx_bytes = ndev->rx_bytes;
    status->tx_bytes = ndev->tx_bytes;
    
    return 0;
}

/*
 * Shutdown network subsystem
 */
void sunpci_net_shutdown(struct sunpci_device *dev)
{
    struct sunpci_net_dev *ndev = dev->net_dev;
    
    if (!ndev)
        return;
    
    /* Stop receive thread */
    if (ndev->rx_running) {
        ndev->rx_running = false;
        if (ndev->rx_thread) {
            kthread_stop(ndev->rx_thread);
            ndev->rx_thread = NULL;
        }
    }
    
    /* Close TAP device */
    net_close_tap(ndev);
    
    pr_info("sunpci: network shutdown, TX=%llu RX=%llu\n",
            ndev->tx_packets, ndev->rx_packets);
    
    kfree(ndev);
    dev->net_dev = NULL;
}
