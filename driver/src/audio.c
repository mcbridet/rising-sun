/*
 * SunPCi driver - Audio subsystem
 *
 * Handles ESS1869 sound card emulation. The guest x86 runs stock ESS
 * drivers which program the ISA DMA controller. The card's firmware
 * traps DMA transfers and writes PCM data to a fixed region in shared
 * memory. This driver reads from that region and can expose it to
 * userspace for playback via ALSA/PulseAudio.
 *
 * Audio data flows:
 *   Guest App → ESS Driver → ISA DMA → Firmware → Shared Memory → Host
 *
 * The ESS1869 supports:
 *   - Sample rates: 5512, 11025, 22050, 44100 Hz
 *   - Formats: 8-bit unsigned, 16-bit signed
 *   - Channels: Mono, Stereo
 *   - DMA: Single-cycle and auto-init modes
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "sunpci.h"
#include "regs.h"
#include "ipc.h"

/*
 * Fixed audio buffer location in shared memory (BAR1)
 * This is set by the SunPCi card's BIOS/firmware
 *
 * Low-latency configuration:
 *   - 16 slots of 4KB each = 64KB total
 *   - At 44.1kHz stereo 16-bit (176.4 KB/s): ~23ms per slot
 *   - With 2-3 slots buffered: 46-69ms total latency
 *   - Doorbell interrupts notify host when slots are ready
 */
#define AUDIO_BUFFER_OFFSET     0x40000     /* After bulk data region */
#define AUDIO_BUFFER_SIZE       0x10000     /* 64KB total */
#define AUDIO_RING_SLOTS        16          /* 16 x 4KB buffers for low latency */
#define AUDIO_SLOT_SIZE         0x1000      /* 4KB per slot (~23ms at 44.1kHz stereo) */
#define AUDIO_RING_MASK         (AUDIO_RING_SLOTS - 1)

/*
 * Audio ring header - at start of audio buffer region
 * Written by guest firmware, read by host
 */
#define AUDIO_HDR_OFFSET        0x00
#define AUDIO_HDR_SIZE          64

/* Header field offsets */
#define AUDIO_HDR_MAGIC         0x00    /* 'AUDI' */
#define AUDIO_HDR_WRITE_PTR     0x04    /* Slot being written (0-15) */
#define AUDIO_HDR_READ_PTR      0x08    /* Slot being read (0-15) */
#define AUDIO_HDR_SAMPLE_RATE   0x0C    /* Current sample rate */
#define AUDIO_HDR_FORMAT        0x10    /* Format flags */
#define AUDIO_HDR_VOLUME_L      0x14    /* Left volume (0-255) */
#define AUDIO_HDR_VOLUME_R      0x18    /* Right volume (0-255) */
#define AUDIO_HDR_STATUS        0x1C    /* Status flags */

/* Audio data starts after header */
#define AUDIO_DATA_OFFSET       (AUDIO_BUFFER_OFFSET + AUDIO_HDR_SIZE)

/* Format flags */
#define AUDIO_FMT_16BIT         (1 << 0)    /* 16-bit samples (vs 8-bit) */
#define AUDIO_FMT_STEREO        (1 << 1)    /* Stereo (vs mono) */
#define AUDIO_FMT_SIGNED        (1 << 2)    /* Signed (vs unsigned) */

/* Status flags */
#define AUDIO_STATUS_PLAYING    (1 << 0)    /* Playback active */
#define AUDIO_STATUS_RECORDING  (1 << 1)    /* Recording active (future) */
#define AUDIO_STATUS_MUTED      (1 << 2)    /* Output muted */

/* Magic value */
#define AUDIO_MAGIC             0x41554449  /* 'AUDI' */

/*
 * Audio IPC commands (SUNPCI_DISP_AUDIO)
 */
#define AUDIO_CMD_INIT          0x0001  /* Initialize audio */
#define AUDIO_CMD_START         0x0002  /* Start playback */
#define AUDIO_CMD_STOP          0x0003  /* Stop playback */
#define AUDIO_CMD_SET_FORMAT    0x0004  /* Set sample rate/format */
#define AUDIO_CMD_SET_VOLUME    0x0005  /* Set volume */
#define AUDIO_CMD_GET_STATUS    0x0006  /* Get current status */
#define AUDIO_CMD_BUFFER_DONE   0x0007  /* Guest: buffer ready for read */

/*
 * Per-device audio state
 */
struct sunpci_audio_state {
    struct sunpci_device *dev;
    
    /* Buffer access */
    void __iomem *buffer_base;      /* Audio region in shmem */
    
    /* Current format */
    u32 sample_rate;
    u32 format;
    u8 volume_left;
    u8 volume_right;
    bool playing;
    bool muted;
    
    /* Ring buffer state (mirror of hardware) */
    u32 write_ptr;                  /* Last seen write pointer */
    u32 read_ptr;                   /* Our read pointer */
    
    /* Statistics */
    u64 samples_played;
    u64 underruns;
    u64 buffers_processed;
    
    spinlock_t lock;
};

/*
 * Read audio header field from shared memory
 */
static u32 audio_read_hdr(struct sunpci_audio_state *audio, u32 offset)
{
    return readl(audio->buffer_base + AUDIO_HDR_OFFSET + offset);
}

/*
 * Write audio header field to shared memory
 */
static void audio_write_hdr(struct sunpci_audio_state *audio, u32 offset, u32 value)
{
    writel(value, audio->buffer_base + AUDIO_HDR_OFFSET + offset);
}

/*
 * Get pointer to audio data slot
 */
static void __iomem *audio_slot_ptr(struct sunpci_audio_state *audio, u32 slot)
{
    return audio->buffer_base + AUDIO_HDR_SIZE + (slot * AUDIO_SLOT_SIZE);
}

/*
 * Check if audio buffer has data available
 */
static bool audio_has_data(struct sunpci_audio_state *audio)
{
    u32 write_ptr = audio_read_hdr(audio, AUDIO_HDR_WRITE_PTR);
    return write_ptr != audio->read_ptr;
}

/*
 * Get number of available audio slots
 */
static u32 audio_available_slots(struct sunpci_audio_state *audio)
{
    u32 write_ptr = audio_read_hdr(audio, AUDIO_HDR_WRITE_PTR);
    return (write_ptr - audio->read_ptr) & AUDIO_RING_MASK;
}

/*
 * Initialize audio subsystem
 */
int sunpci_audio_init(struct sunpci_device *dev)
{
    struct sunpci_audio_state *audio;
    u32 magic;
    
    if (!dev->shmem_base) {
        pr_warn("sunpci%d: no shared memory for audio\n", dev->minor);
        return -ENODEV;
    }
    
    /* Check if shared memory is large enough for audio region */
    if (dev->shmem_len < AUDIO_BUFFER_OFFSET + AUDIO_BUFFER_SIZE) {
        pr_warn("sunpci%d: shmem too small for audio (%llu < %u)\n",
                dev->minor, (unsigned long long)dev->shmem_len,
                AUDIO_BUFFER_OFFSET + AUDIO_BUFFER_SIZE);
        return -ENOMEM;
    }
    
    audio = kzalloc(sizeof(*audio), GFP_KERNEL);
    if (!audio)
        return -ENOMEM;
    
    audio->dev = dev;
    audio->buffer_base = dev->shmem_base + AUDIO_BUFFER_OFFSET;
    spin_lock_init(&audio->lock);
    
    /* Check for audio magic - indicates firmware supports audio */
    magic = audio_read_hdr(audio, AUDIO_HDR_MAGIC);
    if (magic != AUDIO_MAGIC) {
        pr_info("sunpci%d: audio not available (magic=%08x)\n",
                dev->minor, magic);
        /* Not an error - card may not have audio */
        kfree(audio);
        return 0;
    }
    
    /* Read initial state from hardware */
    audio->sample_rate = audio_read_hdr(audio, AUDIO_HDR_SAMPLE_RATE);
    audio->format = audio_read_hdr(audio, AUDIO_HDR_FORMAT);
    audio->volume_left = audio_read_hdr(audio, AUDIO_HDR_VOLUME_L) & 0xFF;
    audio->volume_right = audio_read_hdr(audio, AUDIO_HDR_VOLUME_R) & 0xFF;
    audio->read_ptr = audio_read_hdr(audio, AUDIO_HDR_READ_PTR);
    
    /* Default to 44.1kHz stereo 16-bit if not set */
    if (audio->sample_rate == 0) {
        audio->sample_rate = 44100;
        audio->format = AUDIO_FMT_16BIT | AUDIO_FMT_STEREO | AUDIO_FMT_SIGNED;
    }
    
    dev->audio_state = audio;
    
    pr_info("sunpci%d: audio initialized (%u Hz, %s, %s)\n",
            dev->minor,
            audio->sample_rate,
            (audio->format & AUDIO_FMT_16BIT) ? "16-bit" : "8-bit",
            (audio->format & AUDIO_FMT_STEREO) ? "stereo" : "mono");
    
    return 0;
}

/*
 * Shutdown audio subsystem
 */
void sunpci_audio_shutdown(struct sunpci_device *dev)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio)
        return;
    
    kfree(audio);
    dev->audio_state = NULL;
}

/*
 * Read audio samples from ring buffer
 * Returns number of bytes copied, or negative error
 */
int sunpci_audio_read(struct sunpci_device *dev, void *buffer, size_t size)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    unsigned long flags;
    size_t copied = 0;
    u32 slots_available;
    
    if (!audio)
        return -ENODEV;
    
    spin_lock_irqsave(&audio->lock, flags);
    
    slots_available = audio_available_slots(audio);
    
    while (copied < size && slots_available > 0) {
        void __iomem *slot = audio_slot_ptr(audio, audio->read_ptr);
        size_t to_copy = min(size - copied, (size_t)AUDIO_SLOT_SIZE);
        
        /* Copy from MMIO to buffer */
        memcpy_fromio(buffer + copied, slot, to_copy);
        copied += to_copy;
        
        /* Advance read pointer */
        audio->read_ptr = (audio->read_ptr + 1) & AUDIO_RING_MASK;
        audio_write_hdr(audio, AUDIO_HDR_READ_PTR, audio->read_ptr);
        
        audio->buffers_processed++;
        slots_available--;
    }
    
    audio->samples_played += copied / ((audio->format & AUDIO_FMT_16BIT) ? 2 : 1);
    
    spin_unlock_irqrestore(&audio->lock, flags);
    
    return copied;
}

/*
 * Get current audio format info
 */
int sunpci_audio_get_format(struct sunpci_device *dev,
                            u32 *sample_rate, u32 *format)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio)
        return -ENODEV;
    
    /* Re-read from hardware in case guest changed it */
    audio->sample_rate = audio_read_hdr(audio, AUDIO_HDR_SAMPLE_RATE);
    audio->format = audio_read_hdr(audio, AUDIO_HDR_FORMAT);
    
    if (sample_rate)
        *sample_rate = audio->sample_rate;
    if (format)
        *format = audio->format;
    
    return 0;
}

/*
 * Set volume levels
 */
int sunpci_audio_set_volume(struct sunpci_device *dev, u8 left, u8 right)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio)
        return -ENODEV;
    
    audio->volume_left = left;
    audio->volume_right = right;
    
    audio_write_hdr(audio, AUDIO_HDR_VOLUME_L, left);
    audio_write_hdr(audio, AUDIO_HDR_VOLUME_R, right);
    
    return 0;
}

/*
 * Get volume levels
 */
int sunpci_audio_get_volume(struct sunpci_device *dev, u8 *left, u8 *right)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio)
        return -ENODEV;
    
    if (left)
        *left = audio->volume_left;
    if (right)
        *right = audio->volume_right;
    
    return 0;
}

/*
 * Check if audio data is available for reading
 */
bool sunpci_audio_data_available(struct sunpci_device *dev)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio)
        return false;
    
    return audio_has_data(audio);
}

/*
 * Handle audio-related IPC messages from guest
 */
int sunpci_audio_handle_message(struct sunpci_device *dev,
                                u16 command,
                                const void *payload, size_t len)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio) {
        pr_debug("sunpci%d: audio message but no audio state\n", dev->minor);
        return -ENODEV;
    }
    
    switch (command) {
    case AUDIO_CMD_START:
        audio->playing = true;
        pr_debug("sunpci%d: audio playback started\n", dev->minor);
        break;
        
    case AUDIO_CMD_STOP:
        audio->playing = false;
        pr_debug("sunpci%d: audio playback stopped\n", dev->minor);
        break;
        
    case AUDIO_CMD_SET_FORMAT:
        if (len >= 8) {
            const __le32 *params = payload;
            audio->sample_rate = le32_to_cpu(params[0]);
            audio->format = le32_to_cpu(params[1]);
            pr_debug("sunpci%d: audio format: %u Hz, flags=%08x\n",
                     dev->minor, audio->sample_rate, audio->format);
        }
        break;
        
    case AUDIO_CMD_SET_VOLUME:
        if (len >= 2) {
            const u8 *vol = payload;
            audio->volume_left = vol[0];
            audio->volume_right = vol[1];
        }
        break;
        
    case AUDIO_CMD_BUFFER_DONE:
        /* Guest has filled a buffer - wake up any waiters */
        pr_debug("sunpci%d: audio buffer ready\n", dev->minor);
        break;
        
    default:
        pr_debug("sunpci%d: unknown audio command %04x\n",
                 dev->minor, command);
        return -EINVAL;
    }
    
    return 0;
}

/*
 * Get audio statistics
 */
void sunpci_audio_get_stats(struct sunpci_device *dev,
                            u64 *samples, u64 *underruns, u64 *buffers)
{
    struct sunpci_audio_state *audio = dev->audio_state;
    
    if (!audio) {
        if (samples) *samples = 0;
        if (underruns) *underruns = 0;
        if (buffers) *buffers = 0;
        return;
    }
    
    if (samples)
        *samples = audio->samples_played;
    if (underruns)
        *underruns = audio->underruns;
    if (buffers)
        *buffers = audio->buffers_processed;
}
