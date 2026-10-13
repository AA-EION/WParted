/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_ENDIAN_H
#define WPARTED_ENDIAN_H

#if defined(_WIN32)
#include <stdlib.h>

#ifndef htobe16
#define htobe16(x) _byteswap_ushort(x)
#endif
#ifndef htole16
#define htole16(x) (x)
#endif
#ifndef be16toh
#define be16toh(x) _byteswap_ushort(x)
#endif
#ifndef le16toh
#define le16toh(x) (x)
#endif

#ifndef htobe32
#define htobe32(x) _byteswap_ulong(x)
#endif
#ifndef htole32
#define htole32(x) (x)
#endif
#ifndef be32toh
#define be32toh(x) _byteswap_ulong(x)
#endif
#ifndef le32toh
#define le32toh(x) (x)
#endif

#ifndef htobe64
#define htobe64(x) _byteswap_uint64(x)
#endif
#ifndef htole64
#define htole64(x) (x)
#endif
#ifndef be64toh
#define be64toh(x) _byteswap_uint64(x)
#endif
#ifndef le64toh
#define le64toh(x) (x)
#endif

#else
#include <endian.h>
#endif

#endif // WPARTED_ENDIAN_H
