# 🌄 Rising Sun
### A SunPCi (I) front-end for Linux

Rising Sun is a Rust re-implementation and reverse engineering of the Sun (Oracle) SunPCi software for Solaris on SPARC architectures. It provides a Linux kernel module and a Qt-based frontend for interfacing / interacting with the cards.

#### Supported Cards
- [SunPCi I](https://www.zx.net.nz/computers/sun/cards/pci/SunPCi/SunPCi) (Codename Penguin) - P/N 375-0075, 375-0095.
- I don't have any others to try and get working sorry. :(

#### Features (will expand later)
- Display Passthrough (INT10h, GDI, Direct Frame Buffer Access)
- Audio Passthrough (**not implemented on the original version**)
- Virtual Disk Management (.diskimage, SunPCi-specific format)
- Virtual ISO and Floppy
- Networking Support (TUN)
- Bi-directional Clipboard
- Folder Redirection (using VFS)


#### Stack
- Rust (2024) + Qt5 (may switch to GTK2/3 later, QML is quick and dirty) front-end.
- Kernel module "back-end".
- Low latency, ring buffered audio using `cpal`


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