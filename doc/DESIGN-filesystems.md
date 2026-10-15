# Linux and macOS file systems on Windows: design

Status: implemented for WParted 0.2.0 (see the phase notes at the end).

## Rules

1. Windows formats (NTFS, FAT12/16/32, exFAT, ReFS, UDF) are handled only by
   Windows (`WinNativeFS`, `WinVolumeOps`).  They are never routed to WSL or to
   WParted's helper programs.  `CapabilityResolver` and `CommandRouter` both
   enforce this and unit tests check it.
2. WParted ships no kernel drivers and never suggests unsigned drivers,
   disabling Secure Boot, test signing or code integrity policy changes.  The
   only optional third-party driver used is WinFsp, installed by the user from
   its official signed installer; WParted verifies its Authenticode signature
   before using it.
3. Linux file system logic only comes from upstream code: the Linux kernel
   (through WSL) or upstream tools (e2fsprogs) built from pinned, hash-checked
   sources (`third_party/sources.lock`).
4. macOS file systems are read-only.  The helper that reads them has no write
   code path, and its broker channel is opened read-only.
5. No shell.  Programs get argument vectors.  Nothing is downloaded at run time.

## Components

```
 WParted (elevated GUI)
   |-- WinNativeFS / WinVolumeOps ............ Windows formats (Windows APIs)
   |-- CommandRouter  <- GParted file system classes (ext2.cc, linux_swap.cc,
   |      |              btrfs.cc, xfs.cc, f2fs.cc) call Utils/OperationDetail
   |      |              ::execute_command() as on Linux
   |      |-- Sandbox provider: bundled helper exe (mke2fs, e2fsck, ...)
   |      |       restricted token, low integrity, job object, explicit
   |      |       handle list; disk I/O only through the Broker
   |      `-- WSL provider: wsl.exe -d WParted-Linux -u root --exec
   |              /usr/libexec/wparted/wparted-agent <verb> <args>
   |-- Broker ...................... serves READ/WRITE/FLUSH for one partition
   |                                 extent, optionally read-only
   |-- CapabilityResolver .......... what is possible, by whom, and why not
   `-- AppleReader client ........... wparted-applefs.exe (sandboxed, read-only)
```

### Command router (`WinCommandRouter`)

GParted file system classes build command lines such as
`e2fsck -f -y -v -C 0 '/dev/sdb3'`.  On Windows the router:

1. Tokenises with `Glib::shell_parse_argv` (a tokeniser, no shell).  Commands
   containing shell syntax (pipes, redirection, `;`, `&&`, backquotes) are
   rejected.
2. Looks the program up in the allowlist.  Each entry says which provider runs
   it and whether it may write (per argument pattern, e.g. `e2fsck -n` is
   read-only).
3. Requires exactly the device arguments it expects: each must be a partition
   path from the current scan (`WinScan`), on a disk that is not read-only,
   whose file system type is the one the program is for, and that is not in
   use by Windows.  Whole disk paths are refused.
4. Sandbox provider: replaces the device argument with `wparted-broker`,
   starts the bundled helper with a broker for that partition only.
   WSL provider: attaches the disk with `wsl --mount --bare` (once per batch),
   lets `wparted-agent identify` find the matching Linux block device by exact
   size, partition table identity and partition extents, and replaces the
   device argument with that Linux path.  Temporary mount points created by
   GParted are mapped to `/run/wparted/<name>` inside WSL.
5. Streams stdout/stderr back exactly like the Linux code path so GParted's
   progress parsing keeps working.

`Glib::find_program_in_path()` is wrapped (`WinCommandRouter::find_program`)
so the GParted file system classes see exactly the programs the router can
run.

### Broker (`WinBroker`)

Protocol over two anonymous pipes (helper -> WParted requests, WParted ->
helper replies).  Little-endian fixed headers:

```
request : u32 magic 'WPBR' | u32 op | u64 offset | u32 length | u32 reserved
reply   : u32 magic 'WPBA' | i32 status | u32 length | u32 reserved | data
ops     : 1 INFO (reply: u64 size, u32 sector size, u32 flags)
          2 READ  3 WRITE (data follows request)  4 FLUSH  5 CLOSE
```

Offsets are relative to the partition start.  The server refuses anything
outside `[0, length)`, writes when read-only, lengths above 16 MiB and
unaligned writes (the client does read-modify-write).  The server core
(`BrokerServer::handle`) is platform independent and unit tested with the
in-memory disk.

### Sandbox (`WinSandbox`)

`CreateRestrictedToken(DISABLE_MAX_PRIVILEGE)` with the Administrators group
set deny-only, low mandatory integrity level, `CreateProcessAsUser` with
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` (only the broker pipes and the output
pipes), a job object with kill-on-close, one active process and a memory limit.
The integration test checks that a sandboxed helper cannot open
`\\.\PhysicalDrive0`.

### WSL provider (`WinWsl`)

* Distribution `WParted-Linux`, imported from `share/wparted/wsl/wparted-linux.tar`
  after checking its SHA-256 (`share/wparted/wsl/wparted-linux.sha256`).  The
  root file system is built in CI from a pinned Alpine Linux image and pinned
  packages, with `wsl.conf` turning off Windows drive mounting, interop and
  `PATH` import.
* `wparted-agent` (static C program in the image) is the only entry point.  It
  validates every device argument against the identified disk, only runs
  allowlisted tools, and mounts with `nosuid,nodev,noexec`.
* Eligibility (reason codes shown to the user): WSL missing or without
  `--mount`, Windows on the disk, removable/USB flash/SD, Windows volumes on the
  disk in use, file system not in the WSL kernel, disk read-only for WParted.
* A journal (`%ProgramData%\WParted\wsl-attached.txt`) records attached disks;
  they are detached on exit and on the next start after a crash.
* Experimental: off by default (Preferences), because GitHub's hosted Windows
  runners cannot run WSL2, so the Windows side of this path is not covered by
  CI.  The Linux side (agent and tools) is tested in CI against loop devices.

### Apple reader (`wparted-applefs.exe`)

Sandboxed helper linking hfsfuse's libhfs/libhfsuser (MIT/BSD) and apfs-fuse's
ApfsLib (GPL-2.0-or-later) with a broker device.  Read-only broker.  Verbs:
`info`, `list <path>`, `read <path>` (streams file data).  WParted validates
every path it receives before writing extracted files (no `..`, no absolute
paths, Windows-invalid names replaced, length limits).

### Capability resolver (`WinCapabilities`)

`resolve(fs, op, disk context, components)` returns state, provider, reason
and fix.  The GUI (menus, partition list badges, Information dialog, New and
Format dialogs, File System Support dialog) only reads this.  A table driven
test pins the matrix.

## Phase notes (0.2.0)

* A, B, E, F: implemented and covered by CI on Windows runners.
* C: browse and copy-out implemented; WinFsp drive letters not in 0.2.0.
* D: implemented behind the experimental switch; Linux side tested in CI.
* G (fuse2fs read/write): not started, waiting for upstream.
* LUKS and LVM operations: detection and details only.
