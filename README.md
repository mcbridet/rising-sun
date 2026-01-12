# 🌄 Rising Sun
### A SunPCi (I) front-end for Linux

Rising Sun is a re-implementation and reverse engineering of the Sun (Oracle) SunPCi host driver / front-end for Solaris on SPARC architectures. It provides a Linux kernel module and a Qt-based frontend for interfacing / interacting with the cards.

#### Supported Cards
- [SunPCi I](https://www.zx.net.nz/computers/sun/cards/pci/SunPCi/) (Codename Penguin) - P/N 375-0075, 375-0095.
- I don't have any others to try and get working sorry. :(

#### What works (will expand later)
- Display Passthrough (INT10h, GDI, Direct Frame Buffer Access)
- PS/2 Keyboard and Mouse Emulation
- Audio Passthrough (**a new feature not present on the Sun version**, using spare BAR space)
- Virtual Disk Management (.diskimage, SunPCi-specific format, mostly just Sun adding a magic number in the MBR)
- Virtual CD-ROM (ISO9660 / SCSI-2 / INT13h) and 2x Virtual Floppy Drives. 
- Power Management (Start/Stop/Suspend)
- Networking Support (TUN)
- Bi-directional Clipboard
- Folder Redirection (using VFS)
- Other INT13h interrupt handling

#### What doesn't
- BIOS Flashing.
- Card-level PCI Power Management.
- Fast disk emulation (Using the `emdisk` NT/2k driver instead of INT13h).
- Resizing virtual disk images.
- Converting non-SunPCi disk images to/from the format.
- Good performance, I just haven't tested it much yet.

#### Stack
- A Rust + Qt5 front-end (may switch to GTK2/3
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
---

This project is a WIP, don't use in production (why would you anyway?).