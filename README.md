# 🌄 Rising Sun
### A SunPCi (I) front-end for Linux

Rising Sun is a re-implementation and reverse engineering of the Sun (Oracle) SunPCi host driver / front-end for Solaris on SPARC architectures. It provides a Linux kernel module and a Qt-based frontend for interfacing / interacting with the cards.

#### Supported Cards
- [SunPCi Penguin (I)](https://www.zx.net.nz/computers/sun/cards/pci/SunPCi/) - P/N 375-0075, 375-0095, 32-bit PCI.

#### Unsupported Cards
In the future I hope to support these cards, I just need to grab find them online first.
- [SunPCi Chimera (II/IIpro)](https://theretroweb.com/motherboards/s/sun-sunpci-ii) - P/N 375-0131, 375-3051 - 64-bit PCI-X.
- [SunPCi Bellerophon (III/IIIpro)](https://wiki.preterhuman.net/SunPCi_III), - P/N 375-3116, 375-3203 - 64-bit PCI-X.

#### What works (will expand later)
- Display pass-through (INT10h, GDI and direct frame-buffer access)
- Audio pass-through (**a new feature not present on the Sun version**, using spare BAR space)
- Virtual disk management (.diskimage, SunPCi-specific format, mostly just Sun adding a magic number in the MBR)
- PS/2 keyboard and mouse emulation
- Fast disk access (`emdisk.sys` on Windows NT/2K and the `sundisk` port driver on Windows 9x)
- Named Channel IPC for Windows NT/2K
- Virtual CD-ROM (ISO9660 / SCSI-2 / INT13h) and two virtual floppy drives. 
- Power management (Start/Stop/Suspend)
- Networking (via a Linux TUN device)
- Bi-directional clipboard
- Folder redirection (using VFS)
- Other INT13h interrupt handling

#### What doesn't
- BIOS flashing and patches to fix BIOS issues.
- Card-level PCI power management.
- Resizing virtual disk images.
- Converting non-SunPCi disk images to/from the format.
- Good performance, I just haven't tested it much yet.

#### Stack
- A Rust + Qt5 front-end (may switch to GTK2/3 for improved host compatibility).
- Kernel module (just in C). Tested on Linux ~6.18.
- A Rust common module to bind the two together.

#### Usage
Let me test it out a bit more first.... if you are impatient:
1. Grab [SUNWspci_13.tar.Z](https://ftp.zx.net.nz/pub/software/Solaris/SunPC/SUNWspci_13.tar.Z) and uncompress / extract it, it has important drivers for DOS and/or Windows that you'll need.
2. Make sure you have the SunPCi card installed and appearing in `lspci` as a "Sun Co-Processor".
3. `make` and insert the kernel module in `./driver` via `insmod`.
4. Ensure you have Rust / cargo installed
5. Run `./build.sh`, and then run the `rising-sun` binary.
6. The app might work, or not. Create a blank HDD image and follow the instructions in the SUNWspci_13 package to install DOS/Windows.

**Tip:** Use the Windows 98 drivers for Windows 95. They are a bit newer and offer better performance.

---

This project is a WIP, don't use in production (why would you anyway?).
