# WParted (Windows Partition Editor)

**WParted** is a native Windows port and fork of the official GParted partition editor, designed to provide enterprise-grade partition and storage management on Microsoft Windows.

```
+-----------------------------------------------------------------------------------------+
|  WParted — Native Windows Partition Editor (Forked from GParted)                        |
+-----------------------------------------------------------------------------------------+
|  [ /dev/sda (Disk 0 - NVMe Samsung 990 PRO 2TB)                                     v] |
|  +-----------------------------------------------------------------------------------+  |
|  | [EFI: 100MB] | [MSR: 16MB] |   [C:\ Windows NTFS: 1.8TB]   | [Recovery: 800MB]    |  |
|  +-----------------------------------------------------------------------------------+  |
|  Partition Table:                                                                       |
|  - /dev/sda1 (EFI System Partition - FAT32)        100 MiB    boot, esp                 |
|  - /dev/sda2 (Microsoft Reserved - None)            16 MiB    msftres                   |
|  - /dev/sda3 (C:\ - Windows NTFS)                  1.8 TiB    msftdata                  |
|  - /dev/sda4 (Windows Recovery - NTFS)             800 MiB    diag, hidden              |
|                                                                                         |
|  Pending Operations:                                                                    |
|  - Shrink /dev/sda3 (C:\) from 1.8 TiB to 1.5 TiB (Live Online via VDS)                 |
+-----------------------------------------------------------------------------------------+
```

## Key Highlights

- **Preserved GParted UX Paradigm**: Identical horizontal visual partition bar, color-coded filesystem borders, partition table details list, and pending operations queue pane.
- **Dual Nomenclature System**: Simultaneously displays Windows native paths (`\\.\PhysicalDrive0`, `C:\`) and Linux device paths (`/dev/sda`, `/dev/sda1`) for seamless cross-platform familiarity.
- **Pure Win32 Native Storage Engine (`WParted_Win32_Core`)**: Direct Win32 APIs, `DeviceIoControl`, `IOCTL_DISK_*`, `SetupDi*`, and Virtual Disk Service (VDS) / `StorageManagement.dll`. Zero Cygwin POSIX emulation layer overhead.
- **Live Online Shrink & Extend**: Resize running NTFS and ReFS system volumes (including `C:\`) online without rebooting using VDS and `FSCTL_SHRINK_VOLUME`.
- **BitLocker & VSS Protection**: Automatic BitLocker detection, padlock UI indicators, and transparent key protector suspension during partition operations.
- **WSL2 Linux Filesystem Bridge**: Native management of `ext2/3/4`, `btrfs`, `xfs`, `zfs`, `f2fs`, `linux-swap`, `lvm2 pv`, and `luks` via a transparent WSL2 helper engine (`wparted-wsl-engine`) with real-time progress parsing.
- **Read-Only Apple APFS Support**: Direct container inspection, volume telemetry, and read-only Windows drive letter mounting (`wparted-apfs-mount.exe`) via WinFsp.
- **Boot-Time Native Execution (`wparted-boot.exe`)**: Early boot sector movement and resizing for locked active system partitions using Native Windows Subsystem (`SUBSYSTEM:NATIVE`) and CRC64 journaling.

## Building WParted

### Prerequisites
- Windows 10/11 or Windows Server 2019+
- CMake 3.20+
- Visual Studio 2022 / 2026 (MSVC) or MSYS2 MinGW-w64 (UCRT64)
- Administrator privileges for execution

### Build with CMake & MSVC
```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Build with MSYS2 (UCRT64)
```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gtkmm3
cmake -B build -S . -G Ninja
ninja -C build
```

## Licensing

WParted is free software released under the **GNU General Public License version 2 or later (GPLv2+)**.
Original GParted copyright © 2004-2026 Bart Hakvoort, Curtis Gedak, Mike Fleetwood, and contributors.
WParted Windows port and enhancements © 2026 WParted Contributors.
