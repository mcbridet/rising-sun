/*
 * SunPCi driver internal header
 */

#ifndef _SUNPCI_H
#define _SUNPCI_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/pci.h>

#include "../include/uapi/sunpci_ioctl.h"
#include "ring.h"

#define SUNPCI_DRIVER_NAME "sunpci"
#define SUNPCI_MAX_DEVICES 4

/* PCI Vendor/Device IDs for SunPCi card */
#define SUNPCI_VENDOR_ID    0x108e  /* Sun Microsystems */
#define SUNPCI_DEVICE_ID    0x5043  /* SunPCi ("PC" in ASCII) */

/* Driver version */
#define SUNPCI_VERSION_MAJOR 0
#define SUNPCI_VERSION_MINOR 1
#define SUNPCI_VERSION_PATCH 0

/* Forward declarations */
struct sunpci_device;

/* Forward declaration for storage device */
struct sunpci_storage_dev;

/* Forward declaration for network device */
struct sunpci_net_dev;

/* Forward declarations for display state */
struct sunpci_vga_state;
struct sunpci_video_state;

/* Forward declaration for audio state */
struct sunpci_audio_state;

/* Forward declaration for FSD state */
struct sunpci_fsd_state;

/**
 * struct sunpci_storage - Storage state
 * @disk_path: Paths to mounted disk images
 * @disk_flags: Disk mount flags
 * @cdrom_path: Path to mounted CD-ROM image
 * @floppy_path: Paths to mounted floppy images
 * @disks: Hard disk device contexts
 * @cdrom: CD-ROM device context
 * @floppies: Floppy device contexts
 */
struct sunpci_storage {
    char disk_path[2][SUNPCI_MAX_PATH];
    u32 disk_flags[2];
    char cdrom_path[SUNPCI_MAX_PATH];
    char floppy_path[2][SUNPCI_MAX_PATH];
    
    /* Device contexts for I/O */
    struct sunpci_storage_dev *disks[2];
    struct sunpci_storage_dev *cdrom;
    struct sunpci_storage_dev *floppies[2];
};

/**
 * struct sunpci_display_state - Display state
 * @info: Current display info from guest
 * @config: Display configuration from host
 * @framebuffer: Framebuffer info
 */
struct sunpci_display_state {
    struct sunpci_display_info info;
    struct sunpci_display_config config;
    struct sunpci_framebuffer framebuffer;
};

/**
 * struct sunpci_drive_map - Drive mapping entry
 * @letter: Drive letter (0 if unused)
 * @flags: Mapping flags
 * @path: Host path
 */
struct sunpci_drive_map {
    u8 letter;
    u8 flags;
    char path[SUNPCI_MAX_PATH];
};

/**
 * struct sunpci_device - Per-device structure
 * @dev: Device structure
 * @cdev: Character device
 * @minor: Minor device number
 * @mutex: Device mutex
 * @state: Current session state
 * @start_time: Session start time
 * @config: Session configuration
 * @storage: Storage state
 * @display: Display state
 * @network: Network configuration
 * @clipboard: Current clipboard data
 * @drive_maps: Drive mappings
 * @pdev: PCI device
 * @mmio_base: BAR0 MMIO base address
 * @mmio_len: BAR0 length
 * @shmem_base: BAR1 shared memory base
 * @shmem_len: BAR1 length
 * @cmd_ring: Command ring buffer (host -> guest)
 * @rsp_ring: Response ring buffer (guest -> host)
 * @irq: IRQ number
 * @hw_version: Hardware/firmware version
 */
struct sunpci_device {
    struct device *dev;
    struct cdev cdev;
    int minor;
    struct mutex mutex;
    
    /* Session state */
    enum sunpci_state state;
    ktime_t start_time;
    struct sunpci_session_config config;
    
    /* Subsystems */
    struct sunpci_storage storage;
    struct sunpci_display_state display;
    struct sunpci_network_config network;
    struct sunpci_clipboard clipboard;
    struct sunpci_drive_map drive_maps[SUNPCI_MAX_DRIVE_MAPS];
    struct sunpci_net_dev *net_dev;      /* Network device context */
    struct sunpci_vga_state *vga_state;  /* VGA display state */
    struct sunpci_video_state *video_state; /* Video/GDI state */
    struct sunpci_audio_state *audio_state; /* Audio state */
    struct sunpci_fsd_state *fsd_state;      /* Filesystem redirection */
    
    /* PCI device and resources */
    struct pci_dev *pdev;
    void __iomem *mmio_base;        /* BAR0: Control registers */
    resource_size_t mmio_len;
    void __iomem *shmem_base;       /* BAR1: Shared memory */
    resource_size_t shmem_len;
    
    /* Ring buffers for IPC */
    struct sunpci_ring cmd_ring;    /* Commands: host -> guest */
    struct sunpci_ring rsp_ring;    /* Responses: guest -> host */
    
    /* Interrupt handling */
    int irq;
    
    /* Power management */
    bool suspended;
    
    /* Hardware info */
    u32 hw_version;
};

/* Function prototypes */

/* main.c */
struct sunpci_device *sunpci_create_device(int minor, struct pci_dev *pdev);
void sunpci_destroy_device(struct sunpci_device *dev);

/* pci.c */
int sunpci_pci_init(void);
void sunpci_pci_exit(void);

/* ioctl.c */
long sunpci_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* ipc.c */
int sunpci_ipc_send_cmd(struct sunpci_device *dev,
                        u16 dispatcher, u16 command,
                        const void *payload, size_t payload_len,
                        u32 *seq_out);
int sunpci_ipc_recv_rsp(struct sunpci_device *dev,
                        u32 expected_seq,
                        u16 *status_out,
                        void *payload, size_t payload_len,
                        size_t *actual_len,
                        unsigned long timeout);
int sunpci_ipc_transact(struct sunpci_device *dev,
                        u16 dispatcher, u16 command,
                        const void *cmd_payload, size_t cmd_len,
                        void *rsp_payload, size_t rsp_len,
                        size_t *actual_rsp_len,
                        unsigned long timeout);
int sunpci_ipc_init(struct sunpci_device *dev);
void sunpci_ipc_shutdown(struct sunpci_device *dev);
void sunpci_ipc_handle_responses(struct sunpci_device *dev);

/* mmap.c */
int sunpci_mmap(struct file *file, struct vm_area_struct *vma);
int sunpci_get_fb_info(struct sunpci_device *dev, struct sunpci_framebuffer *info);

/* input.c */
int sunpci_inject_key(struct sunpci_device *dev,
                      const struct sunpci_key_event *event);
int sunpci_inject_mouse(struct sunpci_device *dev,
                        const struct sunpci_mouse_event *event);

/* clipboard.c */
int sunpci_clip_set(struct sunpci_device *dev,
                    const struct sunpci_clipboard *clip);
int sunpci_clip_get(struct sunpci_device *dev,
                    struct sunpci_clipboard *clip);
void sunpci_clip_handle_notify(struct sunpci_device *dev,
                               const void *data, size_t len);

/* storage.c */
struct sunpci_storage_req;
struct sunpci_storage_rsp;
int sunpci_storage_mount_disk(struct sunpci_device *dev,
                              u32 slot, const char *path, u32 flags);
int sunpci_storage_unmount_disk(struct sunpci_device *dev, u32 slot);
int sunpci_storage_mount_cdrom(struct sunpci_device *dev, const char *path);
int sunpci_storage_eject_cdrom(struct sunpci_device *dev);
int sunpci_storage_mount_floppy(struct sunpci_device *dev,
                                u32 drive, const char *path);
int sunpci_storage_eject_floppy(struct sunpci_device *dev, u32 drive);
int sunpci_storage_handle_request(struct sunpci_device *dev,
                                  const struct sunpci_storage_req *req,
                                  struct sunpci_storage_rsp *rsp,
                                  void *data_buf, size_t data_len);
void sunpci_storage_cleanup(struct sunpci_device *dev);

/* network.c */
struct sunpci_net_req;
struct sunpci_net_rsp;
int sunpci_net_init(struct sunpci_device *dev);
int sunpci_net_configure(struct sunpci_device *dev,
                         const struct sunpci_network_config *config);
int sunpci_net_get_status(struct sunpci_device *dev,
                          struct sunpci_network_status *status);
int sunpci_net_handle_request(struct sunpci_device *dev,
                              const struct sunpci_net_req *req,
                              struct sunpci_net_rsp *rsp,
                              void *data_buf, size_t data_len);
void sunpci_net_notify_rx(struct sunpci_device *dev);
void sunpci_net_shutdown(struct sunpci_device *dev);

/* vga.c */
int sunpci_vga_init(struct sunpci_device *dev);
void sunpci_vga_shutdown(struct sunpci_device *dev);
int sunpci_vga_handle_message(struct sunpci_device *dev,
                              u16 command,
                              const void *payload, size_t len,
                              void *response, size_t *rsp_len);
int sunpci_vga_get_info(struct sunpci_device *dev,
                        struct sunpci_display_info *info);
int sunpci_vga_get_palette(struct sunpci_device *dev, u32 *palette, size_t count);
bool sunpci_vga_get_dirty(struct sunpci_device *dev,
                          u16 *x, u16 *y, u16 *w, u16 *h);
void sunpci_vga_mark_dirty_region(struct sunpci_device *dev,
                                  u16 x, u16 y, u16 w, u16 h);

/* video.c */
int sunpci_video_init(struct sunpci_device *dev);
void sunpci_video_shutdown(struct sunpci_device *dev);
int sunpci_video_handle_message(struct sunpci_device *dev,
                                u16 command,
                                const void *payload, size_t len,
                                void *response, size_t *rsp_len);

/* audio.c */
int sunpci_audio_init(struct sunpci_device *dev);
void sunpci_audio_shutdown(struct sunpci_device *dev);
int sunpci_audio_handle_message(struct sunpci_device *dev,
                                u16 command,
                                const void *payload, size_t len);
int sunpci_audio_read(struct sunpci_device *dev, void *buffer, size_t size);
int sunpci_audio_get_format(struct sunpci_device *dev, u32 *sample_rate, u32 *format);
int sunpci_audio_set_volume(struct sunpci_device *dev, u8 left, u8 right);
int sunpci_audio_get_volume(struct sunpci_device *dev, u8 *left, u8 *right);
bool sunpci_audio_data_available(struct sunpci_device *dev);
void sunpci_audio_get_stats(struct sunpci_device *dev, u64 *samples, u64 *underruns, u64 *buffers);

/* fsd.c */
int sunpci_fsd_init(struct sunpci_device *dev);
void sunpci_fsd_shutdown(struct sunpci_device *dev);
int sunpci_fsd_handle_message(struct sunpci_device *dev,
                              u16 command,
                              const void *payload, size_t len,
                              void *response, size_t *rsp_len);
void sunpci_fsd_get_stats(struct sunpci_device *dev,
                          u64 *opened, u64 *closed,
                          u64 *read, u64 *written);

/* Exported symbols */
extern struct class *sunpci_class;
extern int sunpci_major;
extern const struct file_operations sunpci_fops;

#endif /* _SUNPCI_H */
