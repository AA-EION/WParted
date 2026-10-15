/* Copyright (C) 2026 WParted contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "wpbroker.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

#define REQUEST_MAGIC 0x52425057u
#define REPLY_MAGIC   0x41425057u
#define MAX_TRANSFER  (16u * 1024 * 1024)
/* Chunk size used by the client; well below the server maximum. */
#define CHUNK         (4u * 1024 * 1024)

enum { OP_INFO = 1, OP_READ = 2, OP_WRITE = 3, OP_FLUSH = 4, OP_CLOSE = 5 };
enum { ST_OK = 0, ST_PROTOCOL = -1, ST_RANGE = -2, ST_READ_ONLY = -3, ST_IO = -4,
       ST_ALIGNMENT = -5, ST_TOO_BIG = -6, ST_TRANSPORT = -100, ST_NOMEM = -101 };

struct wpb_client
{
	intptr_t rd;
	intptr_t wr;
	uint64_t size;
	uint32_t sector_size;
	uint32_t flags;
};


static int read_exact(intptr_t h, void* buf, size_t len)
{
	unsigned char* p = (unsigned char*)buf;
	while (len)
	{
#if defined(_WIN32)
		DWORD n = 0;
		DWORD want = len > 0x40000000u ? 0x40000000u : (DWORD)len;
		if (! ReadFile((HANDLE)h, p, want, &n, NULL) || n == 0)
			return -1;
#else
		ssize_t n = read((int)h, p, len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
#endif
		p += n;
		len -= (size_t)n;
	}
	return 0;
}


static int write_all(intptr_t h, const void* buf, size_t len)
{
	const unsigned char* p = (const unsigned char*)buf;
	while (len)
	{
#if defined(_WIN32)
		DWORD n = 0;
		DWORD want = len > 0x40000000u ? 0x40000000u : (DWORD)len;
		if (! WriteFile((HANDLE)h, p, want, &n, NULL) || n == 0)
			return -1;
#else
		ssize_t n = write((int)h, p, len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
#endif
		p += n;
		len -= (size_t)n;
	}
	return 0;
}


static void put32(unsigned char* p, uint32_t v)
{
	int i;
	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(v >> (8 * i));
}


static void put64(unsigned char* p, uint64_t v)
{
	int i;
	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)(v >> (8 * i));
}


static uint32_t get32(const unsigned char* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static uint64_t get64(const unsigned char* p)
{
	return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}


/* One request/reply exchange.  'out' receives up to 'out_len' reply bytes. */
static int exchange(wpb_client* c, uint32_t op, uint64_t offset, uint32_t length,
                    const void* data, void* out, uint32_t out_len)
{
	unsigned char req[24], rep[16];
	uint32_t magic, rlen;
	int32_t status;

	put32(req, REQUEST_MAGIC);
	put32(req + 4, op);
	put64(req + 8, offset);
	put32(req + 16, length);
	put32(req + 20, 0);
	if (write_all(c->wr, req, sizeof(req)) != 0)
		return ST_TRANSPORT;
	if (op == OP_WRITE && length && write_all(c->wr, data, length) != 0)
		return ST_TRANSPORT;
	if (read_exact(c->rd, rep, sizeof(rep)) != 0)
		return ST_TRANSPORT;
	magic  = get32(rep);
	status = (int32_t)get32(rep + 4);
	rlen   = get32(rep + 8);
	if (magic != REPLY_MAGIC || rlen > out_len)
		return ST_PROTOCOL;
	if (rlen && read_exact(c->rd, out, rlen) != 0)
		return ST_TRANSPORT;
	if (status == ST_OK && (op == OP_READ || op == OP_INFO) && rlen != out_len)
		return ST_PROTOCOL;
	return status;
}


wpb_client* wpb_connect(intptr_t read_handle, intptr_t write_handle)
{
	unsigned char info[16];
	wpb_client* c = (wpb_client*)calloc(1, sizeof(*c));
	if (! c)
		return NULL;
	c->rd = read_handle;
	c->wr = write_handle;
	if (exchange(c, OP_INFO, 0, 0, NULL, info, sizeof(info)) != ST_OK)
	{
		free(c);
		return NULL;
	}
	c->size        = get64(info);
	c->sector_size = get32(info + 8);
	c->flags       = get32(info + 12);
	if (c->sector_size == 0 || c->sector_size > 65536 || (c->sector_size & (c->sector_size - 1)))
	{
		free(c);
		return NULL;
	}
	return c;
}


wpb_client* wpb_connect_env(void)
{
	const char* env = getenv("WPARTED_BROKER");
	char* end = NULL;
	long long rd, wr;
	if (! env)
		return NULL;
	rd = strtoll(env, &end, 10);
	if (! end || *end != ',')
		return NULL;
	wr = strtoll(end + 1, &end, 10);
	if (! end || *end != '\0' || rd <= 0 || wr <= 0)
		return NULL;
	return wpb_connect((intptr_t)rd, (intptr_t)wr);
}


uint64_t wpb_size(const wpb_client* c)         { return c->size; }
uint32_t wpb_sector_size(const wpb_client* c)  { return c->sector_size; }
int      wpb_read_only(const wpb_client* c)    { return (c->flags & 1) != 0; }


int wpb_read(wpb_client* c, uint64_t offset, void* buffer, size_t length)
{
	unsigned char* p = (unsigned char*)buffer;
	if (offset > c->size || length > c->size - offset)
		return ST_RANGE;
	while (length)
	{
		uint32_t n = length > CHUNK ? CHUNK : (uint32_t)length;
		int st = exchange(c, OP_READ, offset, n, NULL, p, n);
		if (st != ST_OK)
			return st;
		p += n;
		offset += n;
		length -= n;
	}
	return ST_OK;
}


/* Whole sector write of an aligned range. */
static int write_aligned(wpb_client* c, uint64_t offset, const unsigned char* p, size_t length)
{
	while (length)
	{
		uint32_t n = length > CHUNK ? CHUNK : (uint32_t)length;
		int st = exchange(c, OP_WRITE, offset, n, p, NULL, 0);
		if (st != ST_OK)
			return st;
		p += n;
		offset += n;
		length -= n;
	}
	return ST_OK;
}


int wpb_write(wpb_client* c, uint64_t offset, const void* buffer, size_t length)
{
	const unsigned char* p = (const unsigned char*)buffer;
	const uint32_t ss = c->sector_size;
	uint64_t head, tail;
	if (c->flags & 1)
		return ST_READ_ONLY;
	if (offset > c->size || length > c->size - offset)
		return ST_RANGE;
	if (length == 0)
		return ST_OK;

	/* Unaligned start: read-modify-write the first sector. */
	head = offset % ss;
	if (head)
	{
		unsigned char* sec = (unsigned char*)malloc(ss);
		uint64_t base = offset - head;
		size_t n = ss - head < length ? ss - head : length;
		int st;
		if (! sec)
			return ST_NOMEM;
		st = wpb_read(c, base, sec, ss);
		if (st == ST_OK)
		{
			memcpy(sec + head, p, n);
			st = write_aligned(c, base, sec, ss);
		}
		free(sec);
		if (st != ST_OK)
			return st;
		p += n;
		offset += n;
		length -= n;
		if (length == 0)
			return ST_OK;
	}

	/* Aligned middle. */
	tail = length % ss;
	if (length - tail)
	{
		int st = write_aligned(c, offset, p, (size_t)(length - tail));
		if (st != ST_OK)
			return st;
		p += length - tail;
		offset += length - tail;
	}

	/* Unaligned end. */
	if (tail)
	{
		unsigned char* sec = (unsigned char*)malloc(ss);
		int st;
		if (! sec)
			return ST_NOMEM;
		st = wpb_read(c, offset, sec, ss);
		if (st == ST_OK)
		{
			memcpy(sec, p, (size_t)tail);
			st = write_aligned(c, offset, sec, ss);
		}
		free(sec);
		if (st != ST_OK)
			return st;
	}
	return ST_OK;
}


int wpb_flush(wpb_client* c)
{
	return exchange(c, OP_FLUSH, 0, 0, NULL, NULL, 0);
}


void wpb_close(wpb_client* c)
{
	if (! c)
		return;
	exchange(c, OP_CLOSE, 0, 0, NULL, NULL, 0);
	free(c);
}


const char* wpb_strerror(int status)
{
	switch (status)
	{
		case ST_OK:         return "success";
		case ST_PROTOCOL:   return "broker protocol error";
		case ST_RANGE:      return "access outside of the partition";
		case ST_READ_ONLY:  return "the partition is read-only for this operation";
		case ST_IO:         return "disk input/output error";
		case ST_ALIGNMENT:  return "unaligned write";
		case ST_TOO_BIG:    return "transfer too large";
		case ST_TRANSPORT:  return "connection to WParted lost";
		case ST_NOMEM:      return "out of memory";
		default:            return "unknown broker error";
	}
}
