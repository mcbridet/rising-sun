# Network Emulation Analysis

## Overview

The SunPCi network subsystem provides Ethernet connectivity to the guest operating system by bridging the guest's virtual NIC to the host's physical network interface. The guest sees a virtual Ethernet adapter that communicates with the host via the NDIS (Network Driver Interface Specification) protocol layer.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Guest (x86 Windows/DOS)                        │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    TCP/IP Stack / Applications                      │ │
│  └───────────────────────────────┬────────────────────────────────────┘ │
│                                  │                                       │
│  ┌───────────────────────────────┼────────────────────────────────────┐ │
│  │        NDIS Protocol Layer    │                                    │ │
│  │        (ndis2, ndis3, ndis5)  │                                    │ │
│  └───────────────────────────────┼────────────────────────────────────┘ │
│                                  │                                       │
│  ┌───────────────────────────────┴────────────────────────────────────┐ │
│  │                    Guest NDIS Driver                                │ │
│  ├────────────────┬─────────────────────┬─────────────────────────────┤ │
│  │    DOS         │    Windows 95/98    │     Windows NT/2000          │ │
│  │  (sunpcnet.exe)│  sunwndis.vxd       │  sunndis.sys                 │ │
│  │                │  (NDIS 3.x MAC)     │  (NDIS 5.x Miniport)         │ │
│  └────────────────┴─────────────────────┴─────────────────────────────┘ │
│                                  │                                       │
│                    IPC Protocol  │ (via bridge driver)                  │
│                                  │                                       │
└──────────────────────────────────┼───────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Host (SPARC/Solaris)                             │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                       sunpcbinary                                 │   │
│  │                                                                   │   │
│  │  ┌────────────────────────────────────────────────────────────┐  │   │
│  │  │                  libndis95.so.1.1                           │  │   │
│  │  │                  (NDIS Protocol Dispatcher)                 │  │   │
│  │  │                                                             │  │   │
│  │  │  ┌──────────────┐  ┌────────────────────────────────────┐  │  │   │
│  │  │  │ NDIS Protocol│  │ Network Virtual Layer (NVL)        │  │  │   │
│  │  │  │ Dispatcher   │  │                                    │  │  │   │
│  │  │  │              │  │  nvl_net_open    nvl_net_close     │  │  │   │
│  │  │  │ ProcNDISInit │  │  nvl_net_send    nvl_net_recv      │  │  │   │
│  │  │  │ ProcNDISSend │  │  nvl_net_mcast   nvl_net_msap      │  │  │   │
│  │  │  │ ProcNDISRecv │  │                                    │  │  │   │
│  │  │  │ ProcNDISMcast│  │                                    │  │  │   │
│  │  │  └──────────────┘  └────────────────────────────────────┘  │  │   │
│  │  └─────────────────────────────────┬──────────────────────────┘  │   │
│  │                                    │                             │   │
│  │                                    ▼                             │   │
│  │  ┌────────────────────────────────────────────────────────────┐  │   │
│  │  │              Solaris Network Interface                      │  │   │
│  │  │              (NIT / DLPI / Socket)                          │  │   │
│  │  │                                                             │  │   │
│  │  │  ┌────────────────────────────────────────────────────┐    │  │   │
│  │  │  │  Physical Interface (hme0, eri0, ce0, etc.)        │    │  │   │
│  │  │  │  Configured via NVL_INTERFACE environment          │    │  │   │
│  │  │  └────────────────────────────────────────────────────┘    │  │   │
│  │  └────────────────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Analysis

### 1. Host Library (libndis95.so.1.1)

| Property | Value |
|----------|-------|
| Size | 29,200 bytes (~29 KB) |
| Location | `lib/libndis95.so.1.1` |
| Build Date | Oct 4, 2000 |
| Source Files | `NDISDispatcher.c`, `ndis.c`, `net.c`, `queue.c`, `stubs.c`, `sas.c` |
| Compiler | WorkShop Compilers 4.2 (Oct 1996) |
| Build Flags | `-DNVL_USEEREG` (uses EREG, not NIT) |

#### Exported Functions

| Symbol | Address | Purpose |
|--------|---------|---------|
| `NewNDISDispatcher` | 0x23b4 | Create NDIS protocol dispatcher |
| `ndis_entry` | 0x2a10 | Main entry point for NDIS operations |
| `ndis_init` | 0x25c4 | Initialize NDIS subsystem |
| `ndis_close` | 0x2578 | Close NDIS subsystem |
| `ndis_mcast` | 0x276c | Set multicast addresses |
| `ndis_int_on` | 0x291c | Enable interrupts |
| `ndis_int_rel` | 0x2858 | Release interrupt |
| `ndis_int_blk` | 0x28a8 | Block interrupts |
| `ndis_int_unblk` | 0x28e0 | Unblock interrupts |
| `ndis_smb_init` | 0x2958 | Initialize send message buffer |
| `ndis_rmb_init` | 0x2994 | Initialize receive message buffer |
| `net_init` | 0x2b04 | Initialize network layer |
| `GetEnetAddr` | 0x2c74 | Get Ethernet MAC address |

#### Network Virtual Layer (NVL) Functions

| Function | Address | Purpose |
|----------|---------|---------|
| `nvl_net_close` | 0x2cd8 | Close network interface |
| `nvl_net_mcast` | 0x2e58 | Set multicast filter |
| `nvl_net_msap` | 0x2df4 | Set MSAP (MAC Service Access Point) |
| `nvl_net_send` | 0x2eb4 | Send packet |
| `nvl_net_recv` | 0x3080 | Receive packet |
| `nvl_net_sig_on` | 0x31b8 | Enable signal notifications |

#### NVL Global Variables

| Variable | Address | Purpose |
|----------|---------|---------|
| `nvl_net_if_fd` | 0x13efc | Network interface file descriptor |
| `nvl_net_interface` | 0x13f6c | Interface name |
| `nvl_net_ethertype` | 0x13f76 | Ethernet type filter |
| `nvl_net_pkttype` | 0x13f60 | Packet type filter |
| `int_ready` | 0x13ef4 | Interrupt ready flag |

#### Dispatch Tables

| Table | Purpose |
|-------|---------|
| `NativeDispatchTable` | Normal operation |
| `SwappedDispatchTable` | Byte-swapped (endian conversion) |

#### Protocol Operations

| Operation | Function | Description |
|-----------|----------|-------------|
| Noop | `ProcNoop` | No operation |
| Init | `ProcNDISInit` | Initialize NDIS adapter |
| Send | `ProcNDISSend` | Transmit packet |
| Recv | `ProcNDISRecv` | Receive packet |
| DataReady | `ProcNDISDataReady` | Data ready notification (triggers IRQ) |
| Open | `ProcNDISOpen` | Open adapter |
| Close | `ProcNDISClose` | Close adapter |
| Mcast | `ProcNDISMcast` | Set multicast list |
| IntRel | `ProcNDISIntRel` | Release interrupt |

#### Supported Interrupt Numbers

The library supports the following guest IRQ lines for network notifications:

| IRQ | Debug Message |
|-----|---------------|
| 9 | "DProcNDISDataReady. Generating IRQ9" |
| 10 | "DProcNDISDataReady. Generating IRQ10" |
| 11 | "DProcNDISDataReady. Generating IRQ11" |
| 15 | "DProcNDISDataReady. Generating IRQ15" |

#### Environment Variables

| Variable | Purpose |
|----------|---------|
| `NVL_INTERFACE` | Host network interface name (e.g., `hme0`) |
| `NDISVERBOSE` | Enable verbose debug output |
| `DEVEXTENSIONS` | Enable device extensions mode |

#### Error Messages

```
SunPCi: NDIS_OPEN: could not initialize NIT
SunPCi: Could not initialize network interface for NDIS operation.
Please check to see that you are using a valid ethernet interface
NDIS Interrupt Number 0x%lx not supported.
Software Would not receive any network packets.
```

### 2. DOS Network Support (sunpcnet.exe)

| Property | Value |
|----------|-------|
| Location | `defaults/7.01/sunpc/sunpcnet.exe` |
| Purpose | Drive mapping and network packet driver |

The DOS `sunpcnet.exe` provides both drive mapping (see filesystem analysis) and basic network packet driver functionality.

### 3. Windows 95/98 NDIS Driver (sunwndis.vxd)

| Property | Value |
|----------|-------|
| Location | `drivers/win95/sunwndis.vxd` |
| Type | VxD NDIS 3.x MAC Driver |
| INF File | `sunwndis.inf` |
| Host Library | `libndis95.so.1.1` |
| Build Tool | VtoolsD |
| Source Path | `G:\net\tbird\disk6h\penguin_source\penguin\sw\intel\win95\ndismac\` |

#### NDIS Version Support

| Version | Interface | Description |
|---------|-----------|-------------|
| NDIS 2.x | `ndis2` | DOS/Windows 3.x compatibility |
| NDIS 3.x | `ndis3` | Windows 95 native |
| ODI | `odi` | Novell NetWare compatibility |

#### INF Configuration

```ini
HKR,,DevLoader,,*ndis
HKR,,DeviceVxDs,,sunwndis.vxd
HKR,NDIS,LogDriverName,,"SUNWNDIS"
HKR,NDIS,MajorNdisVersion,HEX,03
HKR,NDIS,MinorNdisVersion,HEX,0A
HKR,Ndi\Interfaces,DefUpper,,"ndis2,ndis3"
HKR,Ndi\Interfaces,DefLower,,"ethernet"
HKR,Ndi\Interfaces,UpperRange,,"ndis2,ndis3,odi"
HKR,Ndi\Interfaces,LowerRange,,"ethernet"
```

#### IPC Communication

The VxD communicates with the host via IPC:

```
SpcNDISInit - IpcRead failed
SpcNDISInit - IpcWrite failed
```

#### Key Functions (from strings)

| Function | Purpose |
|----------|---------|
| `SUNPCAddAdapter` | Add network adapter |
| `SUNPCOpenAdapter` | Open adapter for use |
| `SpcNDISInit` | Initialize NDIS via IPC |
| `SpcNDISRecv` | Receive packet via IPC |

### 4. Windows NT/2000 NDIS Driver (sunndis.sys)

| Property | Value |
|----------|-------|
| Location (NT4) | `drivers/winnt/oem/$$/system/sunndis/sunndis.sys` |
| Location (W2K) | `drivers/winnt/oem/W2K/net/sunndis.sys` |
| Type | NDIS 5.x Miniport Driver |
| INF File | `sunndis.inf` |
| Source Path | `H:\src\penguin\sw\intel\winnt\ndis\sunndis.c` |

#### NDIS 5.x Configuration

```ini
Signature       = "$Windows NT$"
Class           = Net
ClassGUID       = {4d36e972-e325-11ce-bfc1-08002be10318}
Characteristics = 0x4  ; NCF_PHYSICAL
BusType         = 0
IRQConfig       = 9,10,11,15
```

#### I/O Port Ranges

| Port | Purpose |
|------|---------|
| 0x2e | Unknown (ISA bridge?) |
| 0xf00 | Unknown (device registers?) |

#### NDIS Miniport Functions

| Function | Purpose |
|----------|---------|
| `MiniportRegisterAdapter` | Register adapter with NDIS |
| `NdisMRegisterMiniport` | NDIS miniport registration |
| `NdisMRegisterInterrupt` | Register interrupt handler |
| `NdisMDeregisterInterrupt` | Deregister interrupt |
| `NdisAllocateMemory` | Allocate adapter memory |

### 5. Host Network Interface Configuration

The host network interface is configured via the `NVL_INTERFACE` environment variable:

```bash
# In sunpci launcher script:
if [ "$NVL_INTERFACE" = "" ]; then
    NVL_INTERFACE=lo0  # Default to loopback for standalone
    # Or auto-detect first physical interface
fi
export NVL_INTERFACE
```

**Default interface:** `hme0` (Sun Fast Ethernet)

**Multi-interface warning:**
```
SunPCi: This machine has multiple network interfaces. If you are using
       SunPC networking, you need to set the environment variable NVL_INTERFACE
       to indicate the desired network interface. Type netstat -i for a list
```

## Protocol Details

### Packet Format

Ethernet frames are passed between guest and host with minimal encapsulation:

```
┌─────────────────────────────────────────────────┐
│  Request Header                                 │
│  ┌──────────────┬───────────────────────────┐  │
│  │ Opcode (4B)  │ Packet Length (4B)        │  │
│  └──────────────┴───────────────────────────┘  │
├─────────────────────────────────────────────────┤
│  Ethernet Frame                                 │
│  ┌────────────────────────────────────────────┐ │
│  │ Dest MAC (6B) │ Src MAC (6B) │ Type (2B)  │ │
│  ├────────────────────────────────────────────┤ │
│  │ Payload (46-1500 bytes)                    │ │
│  └────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### Multicast Support

The library supports multicast address filtering:

```
libndis: New MCAST Request. NumEntries = %d
libndis: Error Setting Multicast Addresses
```

Multicast configuration via:
- `nvl_net_mcast` - Set multicast filter list
- `SPCIO_MCAST` - ioctl for multicast configuration

### Interrupt Handling

Network receive generates interrupt to guest:

```c
// Interrupt flow:
1. Host receives packet on physical interface
2. nvl_net_recv() reads packet
3. Packet queued in pktq
4. ProcNDISDataReady() generates guest IRQ
5. Guest ISR reads packet via IPC
```

Queue functions:
- `q_enq` - Enqueue packet
- `q_deq` - Dequeue packet
- `q_len` - Queue length
- `q_empty` - Check if empty
- `q_free` - Free queue

## Implementation Considerations for Rising Sun

### Linux Host Implementation

Replace Solaris NIT/DLPI with Linux equivalents:

| Original (Solaris) | Replacement (Linux) |
|--------------------|---------------------|
| NIT (Network Interface Tap) | `AF_PACKET` socket |
| DLPI | `libpcap` or raw sockets |
| `hme0`, `eri0` | `eth0`, `enp3s0`, etc. |
| `NVL_INTERFACE` env | Same or config file |

#### Recommended Approach: TAP Device

Using a Linux TAP device provides the cleanest abstraction:

```rust
/// Network adapter using TAP device
pub struct TapAdapter {
    /// TAP file descriptor
    tap_fd: RawFd,
    /// MAC address
    mac_addr: [u8; 6],
    /// Receive queue
    rx_queue: VecDeque<Vec<u8>>,
    /// Transmit queue
    tx_queue: VecDeque<Vec<u8>>,
}

impl TapAdapter {
    pub fn new(name: &str) -> Result<Self, io::Error> {
        // Create TAP device via /dev/net/tun
        let fd = open("/dev/net/tun", O_RDWR)?;
        // Configure as TAP (Ethernet) device
        // ...
    }
    
    pub fn send(&self, packet: &[u8]) -> Result<usize, io::Error> {
        write(self.tap_fd, packet)
    }
    
    pub fn recv(&self) -> Result<Vec<u8>, io::Error> {
        let mut buf = vec![0u8; 1514]; // Max Ethernet frame
        let len = read(self.tap_fd, &mut buf)?;
        buf.truncate(len);
        Ok(buf)
    }
}
```

### Protocol Messages (Rising Sun)

```rust
/// NDIS protocol messages
#[derive(Debug, Serialize, Deserialize)]
pub enum NdisMessage {
    /// Initialize adapter
    Init { irq: u8 },
    /// Initialize response with MAC address
    InitResponse { mac: [u8; 6], success: bool },
    /// Open adapter
    Open,
    /// Close adapter
    Close,
    /// Send packet
    Send { data: Vec<u8> },
    /// Receive packet notification
    DataReady,
    /// Receive packet request
    Recv,
    /// Receive packet response
    RecvResponse { data: Vec<u8> },
    /// Set multicast list
    SetMulticast { addresses: Vec<[u8; 6]> },
    /// Release interrupt
    IntRelease,
}
```

### Guest Driver Strategy

| Guest OS | Strategy |
|----------|----------|
| DOS | Packet driver or ODI driver |
| Windows 95/98 | NDIS 3.x MAC driver (VxD) |
| Windows NT/2000 | NDIS 5.x Miniport driver |
| Windows XP+ | NDIS 5.1/6.x Miniport |

### Network Modes

| Mode | Description | Linux Implementation |
|------|-------------|---------------------|
| Bridged | Guest on same network as host | TAP + Bridge |
| NAT | Guest behind NAT | TAP + iptables |
| Host-only | Guest can only reach host | TAP without routing |
| Isolated | No network | No TAP device |

## File Listing

### Host Components
| File | Size | Purpose |
|------|------|---------|
| `lib/libndis95.so.1.1` | 29 KB | NDIS protocol dispatcher |

### Guest Components (DOS)
| File | Purpose |
|------|---------|
| `defaults/7.01/sunpc/sunpcnet.exe` | DOS network utility |

### Guest Components (Windows 95/98)
| File | Purpose |
|------|---------|
| `drivers/win95/sunwndis.vxd` | NDIS 3.x VxD driver |
| `drivers/win95/sunwndis.inf` | Installation INF |
| `drivers/win98/sunwndis.vxd` | Win98 version |
| `drivers/win98/sunwndis.inf` | Win98 INF |

### Guest Components (Windows NT/2000)
| File | Purpose |
|------|---------|
| `drivers/winnt/oem/$$/system/sunndis/sunndis.sys` | NT4 NDIS driver |
| `drivers/winnt/oem/W2K/net/sunndis.sys` | Windows 2000 driver |
| `drivers/winnt/oem/W2K/net/sunndis.inf` | W2K INF |
| `drivers/winnt/oem/net/sunndis/sunndis.sys` | Generic NT driver |
| `drivers/winnt/patch/sunndis.sys` | Patched version |
| `drivers/winnt/oem/W2K/patch/sunndis.sys` | W2K patched |

## Open Questions

1. What is the exact IPC protocol format for packet transmission?
2. How is the MAC address derived (from host interface or synthetic)?
3. What is the maximum MTU supported?
4. How are VLAN tags handled (if at all)?
5. Is promiscuous mode supported?
6. How are link state changes (up/down) communicated?
7. What is the packet queue depth limit?

## References

- Microsoft NDIS Programmer's Reference
- Linux TAP/TUN documentation (`Documentation/networking/tuntap.txt`)
- Solaris DLPI Programmer's Guide
- VtoolsD VxD Development Kit
