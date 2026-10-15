# WParted — Windows Partition Editor

WParted is a port of [GParted](https://gparted.org), the GNOME Partition Editor,
to Microsoft Windows. It keeps GParted's interface and its operation queue
(plan the changes, review them, then apply them), and drives Windows' own
storage stack instead of Linux tools.

> **WParted 0.1.0 is a pre-release.** Partition editing can destroy data.
> Back up anything you care about before applying operations.

## What works on Windows

| Area | Supported in 0.1.0 |
| --- | --- |
| Disks | Basic MBR and GPT disks, internal and USB |
| Partition tables | Create a new GPT or MBR (msdos) table |
| Partitions | Create and delete partitions, MBR extended and logical partitions, GPT partition names, flags (`esp`, `msftdata`, `msftres`, `diag`, `hidden`, `no_automount`, `legacy_boot`, `boot`, `lba`, …) |
| NTFS | Create (quick format), label, check and repair (`chkdsk`), grow and shrink — also while mounted, through the Windows Storage Management API (the same mechanism as Disk Management) |
| FAT16, FAT32, exFAT | Create (quick format), label, check and repair (`chkdsk`) |
| Drive letters | *Assign/Remove Drive Letter* takes the place of GParted's mount and unmount |
| Detection only | ext2/3/4, btrfs, xfs, f2fs, jfs, reiserfs, nilfs2, swap, LVM2 PV, Linux RAID, LUKS, BitLocker, ReFS, HFS/HFS+, APFS, ISO 9660, UDF and others are recognised and shown with their labels and UUIDs, but cannot be changed |

Not supported in 0.1.0, compared with GParted on Linux:

* moving the start of a partition, and copying or pasting partitions;
* any operation on Linux, Apple or encrypted file systems (no WSL helpers);
* opening or closing LUKS encryption, LVM, software RAID and dmraid;
* changing file system UUIDs;
* attempting data rescue.

Menu items for these are disabled. The *File System Support* dialog
(WParted → File System Support) lists exactly what is available.

### Device names

Disks are shown with GParted style names: `/dev/sda` is `\\.\PhysicalDrive0`,
`/dev/sdb` is `\\.\PhysicalDrive1`, and so on. Partition numbers are those of the
partition table (GPT entry number; MBR 1–4 for primaries and 5 upwards for logical
partitions), so `/dev/sda3` is the third GPT entry. Drive letters and mount folders
are shown in the *Mount Point* column. Either style may be given on the command
line: `wparted /dev/sdb` or `wparted \\.\PhysicalDrive1`.

## Safety

WParted runs elevated (administrator) because it opens disks directly. To limit
the damage a bug or a surprise can do:

* **Disks it does not fully understand are read-only**: dynamic disks, Storage
  Spaces, hybrid MBRs, offline or write-protected disks, and any disk whose
  partition table as read from the sectors does not match what Windows reports.
* **Volumes in use by Windows are protected**: the Windows, boot and EFI system
  volumes and volumes with a page, swap or hibernation file are busy, as is any
  volume with a drive letter or mount folder. Only what Windows itself does online
  (NTFS resize, file system label) is offered for them, and the drive letters of
  the Windows and paging volumes cannot be removed. BitLocker volumes are recognised and cannot be
  formatted, resized or checked.
* **Partitions are changed only through Windows** (`IOCTL_DISK_SET_DRIVE_LAYOUT_EX`),
  never by writing partition table sectors directly. Before every write the table
  is read again and compared with what was shown; if another program changed it the
  operation stops. After every write the result is verified both as Windows reports
  it and as raw sectors, and the previous table is restored if they differ.
* **Every disk is identified by its device instance, serial number, size and
  model**, checked again before each change, so a disk renumbered by Windows (for
  example after plugging in a USB disk) cannot receive operations meant for another.
* Volumes on partitions being changed or deleted are locked and dismounted first;
  if Windows cannot lock them the operation stops. Sector writes (clearing old file
  system signatures) are only allowed inside a single partition being changed.
* WParted does not run any external program except Windows' own `chkdsk.exe`
  (by absolute path), so stray Linux tools on the `PATH` (from MSYS2, Cygwin or Git)
  can never be run against Windows disks.

## Install

Download from the [releases page](https://github.com/AA-EION/WParted/releases):

* `WParted-0.1.0-win64-setup.exe` — installer;
* `WParted-0.1.0-win64-portable.zip` — unzip anywhere and run `bin\wparted.exe`.

Windows 10 or 11, 64 bit. `SHA256SUMS.txt` lists the checksums.

The GParted manual (<https://gparted.org/display-doc.php?name=help-manual>)
applies, apart from the differences above.

## Building

WParted is built with MinGW-w64 GCC in an [MSYS2](https://www.msys2.org) UCRT64
shell (gtkmm 3 is not available for MSVC):

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-gtkmm3 mingw-w64-ucrt-x86_64-adwaita-icon-theme \
    mingw-w64-ucrt-x86_64-librsvg mingw-w64-ucrt-x86_64-gettext-tools \
    mingw-w64-ucrt-x86_64-gtk-update-icon-cache
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
scripts/bundle-windows.sh build dist/WParted   # portable folder with the GTK runtime
```

The installer is built from the bundle with
[Inno Setup](https://jrsoftware.org/isinfo.php) 6:
`iscc /DMyAppVersion=0.1.0 installer\wparted.iss`.

### Tests

* `build/wparted_unit_tests` — partition table model, the libparted interface
  over an in-memory disk, and the file system prober. These also build and run on
  Linux: `cmake -S . -B build -DWPARTED_BUILD_GUI=OFF`. With
  `WPARTED_TEST_IMAGES` pointing at the output of `tests/make_test_images.sh`
  they are checked against images made by `sfdisk`, `sgdisk`, `mkfs.*` and the
  results of `blkid`.
* `build/wparted_win_tests` — **destructive** integration tests through the real
  Windows storage stack. They only run on the disk number in
  `WPARTED_TEST_VHD_DISK`, and only if that disk is an attached virtual disk
  (VHD/VHDX) of at most 8 GiB.
* `wparted --smoke-test` — read-only scan of all disks and a short display of
  the main window, used by CI to check a bundle.

Continuous integration runs all of these on GitHub's Windows runners; see
`.github/workflows/build-and-release.yml`.

## Versioning

Releases are numbered `MAJOR.MINOR.PATCH` starting at 0.1.0; the version is set
once, in `CMakeLists.txt`. Releases are published by pushing a tag `v<version>`.

## Credits and license

WParted is free software under the GNU General Public License, version 2 or
later (see `COPYING`).

GParted is copyright © 2004–2026 Bart Hakvoort, Curtis Gedak, Mike Fleetwood and
the GParted contributors listed in `AUTHORS`. The Windows port is copyright ©
2026 Juan E. Gallego T. (Issen Software Group) and the WParted contributors.
WParted is not affiliated with or endorsed by the GParted project. The upstream
GParted README is kept in `README`.
