/* Copyright (C) 2026 WParted contributors
 *
 * This file may be redistributed under the terms of the GNU Library General
 * Public License, version 2, like the rest of libext2fs.
 */

/* wparted_io.c --- libext2fs I/O manager that reads and writes through the
 * WParted disk broker (helpers/broker/wpbroker.h).
 *
 * The e2fsprogs tools bundled with WParted run in a sandbox without access to
 * any disk.  They are given the device name "wparted-broker" and this I/O
 * manager, built in place of unix_io / windows_io, sends every read and write
 * to WParted, which limits it to the one partition being worked on.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

#include "wpbroker.h"

struct wparted_private
{
	errcode_t        magic;
	wpb_client*      client;
	struct struct_io_stats stats;
};

/* One broker connection per process, shared by every channel the tool opens
 * (e2fsck and tune2fs open the device more than once). */
static wpb_client* shared_client;
static int         shared_refs;

static errcode_t wparted_open(const char* name, int flags, io_channel* channel);
static errcode_t wparted_close(io_channel channel);
static errcode_t wparted_set_blksize(io_channel channel, int blksize);
static errcode_t wparted_read_blk64(io_channel channel, unsigned long long block, int count, void* data);
static errcode_t wparted_write_blk64(io_channel channel, unsigned long long block, int count, const void* data);
static errcode_t wparted_read_blk(io_channel channel, unsigned long block, int count, void* data);
static errcode_t wparted_write_blk(io_channel channel, unsigned long block, int count, const void* data);
static errcode_t wparted_flush(io_channel channel);
static errcode_t wparted_write_byte(io_channel channel, unsigned long offset, int size, const void* data);
static errcode_t wparted_set_option(io_channel channel, const char* option, const char* arg);
static errcode_t wparted_get_stats(io_channel channel, io_stats* stats);
static errcode_t wparted_discard(io_channel channel, unsigned long long block, unsigned long long count);
static errcode_t wparted_cache_readahead(io_channel channel, unsigned long long block, unsigned long long count);
static errcode_t wparted_zeroout(io_channel channel, unsigned long long block, unsigned long long count);

static struct struct_io_manager struct_wparted_manager = {
	.magic           = EXT2_ET_MAGIC_IO_MANAGER,
	.name            = "WParted broker I/O Manager",
	.open            = wparted_open,
	.close           = wparted_close,
	.set_blksize     = wparted_set_blksize,
	.read_blk        = wparted_read_blk,
	.write_blk       = wparted_write_blk,
	.flush           = wparted_flush,
	.write_byte      = wparted_write_byte,
	.set_option      = wparted_set_option,
	.get_stats       = wparted_get_stats,
	.read_blk64      = wparted_read_blk64,
	.write_blk64     = wparted_write_blk64,
	.discard         = wparted_discard,
	.cache_readahead = wparted_cache_readahead,
	.zeroout         = wparted_zeroout,
};

io_manager wparted_io_manager = &struct_wparted_manager;


/* Plain file helpers, as in unix_io.c / windows_io.c, which this file
 * replaces.  libext2fs uses them for ordinary files only (undo files,
 * populating a new file system from a directory). */
int ext2fs_open_file(const char* pathname, int flags, mode_t mode)
{
	flags |= O_BINARY;
#if defined(HAVE_OPEN64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
	if (mode)
		return open64(pathname, flags, mode);
	return open64(pathname, flags);
#else
	if (mode)
		return open(pathname, flags, mode);
	return open(pathname, flags);
#endif
}


int ext2fs_stat(const char* path, ext2fs_struct_stat* buf)
{
#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
	return stat64(path, buf);
#else
	return stat(path, buf);
#endif
}


int ext2fs_fstat(int fd, ext2fs_struct_stat* buf)
{
#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
	return fstat64(fd, buf);
#else
	return fstat(fd, buf);
#endif
}


int wparted_io_is_device(const char* name)
{
	return name && strcmp(name, WPB_DEVICE_NAME) == 0;
}


static void close_shared(void)
{
	if (shared_client)
	{
		wpb_flush(shared_client);
		wpb_close(shared_client);
		shared_client = NULL;
	}
}


/* The connection stays open until the process exits: the broker ends the
 * conversation when it is closed. */
static wpb_client* connect_shared(void)
{
	if (! shared_client)
	{
		shared_client = wpb_connect_env();
		if (shared_client)
			atexit(close_shared);
	}
	if (shared_client)
		shared_refs++;
	return shared_client;
}


static void release_shared(void)
{
	if (shared_refs > 0)
		shared_refs--;
	if (shared_client)
		wpb_flush(shared_client);
}


/* ext2fs_get_device_size2() for the broker device. */
errcode_t wparted_io_device_size(int blocksize, blk64_t* retblocks)
{
	wpb_client* c = connect_shared();
	if (! c)
		return EXT2_ET_BAD_DEVICE_NAME;
	*retblocks = wpb_size(c) / (unsigned) blocksize;
	release_shared();
	return 0;
}


/* ext2fs_get_device_sectsize() for the broker device. */
errcode_t wparted_io_sector_size(int* sectsize)
{
	wpb_client* c = connect_shared();
	if (! c)
		return EXT2_ET_BAD_DEVICE_NAME;
	*sectsize = (int) wpb_sector_size(c);
	release_shared();
	return 0;
}


static errcode_t to_errcode(int status)
{
	switch (status)
	{
		case 0:  return 0;
		case -3: return EXT2_ET_RO_FILSYS;
		case -2: return EXT2_ET_LLSEEK_FAILED;
		default: return EIO;
	}
}


static errcode_t wparted_open(const char* name, int flags, io_channel* channel)
{
	io_channel io = NULL;
	struct wparted_private* data = NULL;
	errcode_t retval;

	if (! wparted_io_is_device(name))
	{
		/* Only the broker device: never a path, never another disk. */
		return EXT2_ET_BAD_DEVICE_NAME;
	}

	retval = ext2fs_get_mem(sizeof(struct struct_io_channel), &io);
	if (retval)
		return retval;
	memset(io, 0, sizeof(struct struct_io_channel));
	io->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	retval = ext2fs_get_mem(sizeof(struct wparted_private), &data);
	if (retval)
		goto fail;
	memset(data, 0, sizeof(struct wparted_private));
	data->magic = EXT2_ET_MAGIC_UNIX_IO_CHANNEL;
	data->stats.num_fields = 2;

	data->client = connect_shared();
	if (! data->client)
	{
		retval = EXT2_ET_BAD_DEVICE_NAME;
		goto fail;
	}
	if ((flags & IO_FLAG_RW) && wpb_read_only(data->client))
	{
		release_shared();
		retval = EXT2_ET_RO_FILSYS;
		goto fail;
	}

	retval = ext2fs_get_mem(strlen(name) + 1, &io->name);
	if (retval)
	{
		release_shared();
		goto fail;
	}
	strcpy(io->name, name);
	io->manager = wparted_io_manager;
	io->private_data = data;
	io->block_size = 1024;
	io->read_error = 0;
	io->write_error = 0;
	io->refcount = 1;
	io->flags = CHANNEL_FLAGS_BLOCK_DEVICE;
	*channel = io;
	return 0;

fail:
	if (data)
		ext2fs_free_mem(&data);
	if (io)
		ext2fs_free_mem(&io);
	return retval;
}


static errcode_t wparted_close(io_channel channel)
{
	struct wparted_private* data;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	if (--channel->refcount > 0)
		return 0;
	if (data->client)
	{
		wpb_flush(data->client);
		release_shared();
	}
	ext2fs_free_mem(&channel->private_data);
	if (channel->name)
		ext2fs_free_mem(&channel->name);
	ext2fs_free_mem(&channel);
	return 0;
}


static errcode_t wparted_set_blksize(io_channel channel, int blksize)
{
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	if (blksize <= 0)
		return EXT2_ET_INVALID_ARGUMENT;
	channel->block_size = blksize;
	return 0;
}


/* count > 0: blocks; count < 0: bytes (libext2fs convention). */
static int transfer_size(io_channel channel, int count, size_t* size)
{
	if (count < 0)
		*size = (size_t) -count;
	else
		*size = (size_t) count * (size_t) channel->block_size;
	return 0;
}


static errcode_t wparted_read_blk64(io_channel channel, unsigned long long block, int count, void* buf)
{
	struct wparted_private* data;
	size_t size;
	int st;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	transfer_size(channel, count, &size);
	if (size == 0)
		return 0;
	st = wpb_read(data->client, block * (unsigned long long) channel->block_size, buf, size);
	if (st != 0)
	{
		errcode_t err = to_errcode(st);
		if (channel->read_error)
			return channel->read_error(channel, (unsigned long) block, count, buf, size, 0, err);
		return err;
	}
	data->stats.bytes_read += size;
	return 0;
}


static errcode_t wparted_write_blk64(io_channel channel, unsigned long long block, int count, const void* buf)
{
	struct wparted_private* data;
	size_t size;
	int st;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	transfer_size(channel, count, &size);
	if (size == 0)
		return 0;
	st = wpb_write(data->client, block * (unsigned long long) channel->block_size, buf, size);
	if (st != 0)
	{
		errcode_t err = to_errcode(st);
		if (channel->write_error)
			return channel->write_error(channel, (unsigned long) block, count, buf, size, 0, err);
		return err;
	}
	data->stats.bytes_written += size;
	return 0;
}


static errcode_t wparted_read_blk(io_channel channel, unsigned long block, int count, void* buf)
{
	return wparted_read_blk64(channel, block, count, buf);
}


static errcode_t wparted_write_blk(io_channel channel, unsigned long block, int count, const void* buf)
{
	return wparted_write_blk64(channel, block, count, buf);
}


static errcode_t wparted_write_byte(io_channel channel, unsigned long offset, int size, const void* buf)
{
	struct wparted_private* data;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	if (size <= 0)
		return 0;
	return to_errcode(wpb_write(data->client, offset, buf, (size_t) size));
}


static errcode_t wparted_flush(io_channel channel)
{
	struct wparted_private* data;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	return to_errcode(wpb_flush(data->client));
}


static errcode_t wparted_set_option(io_channel channel, const char* option, const char* arg)
{
	(void) channel;
	(void) option;
	(void) arg;
	return EXT2_ET_INVALID_ARGUMENT;
}


static errcode_t wparted_get_stats(io_channel channel, io_stats* stats)
{
	struct wparted_private* data;
	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (struct wparted_private*) channel->private_data;
	if (stats)
		*stats = &data->stats;
	return 0;
}


static errcode_t wparted_discard(io_channel channel, unsigned long long block, unsigned long long count)
{
	(void) channel;
	(void) block;
	(void) count;
	/* Never discard: callers then write zeros where they need them. */
	return EXT2_ET_UNIMPLEMENTED;
}


static errcode_t wparted_cache_readahead(io_channel channel, unsigned long long block, unsigned long long count)
{
	(void) channel;
	(void) block;
	(void) count;
	return 0;
}


static errcode_t wparted_zeroout(io_channel channel, unsigned long long block, unsigned long long count)
{
	/* libext2fs falls back to writing zero blocks itself. */
	(void) channel;
	(void) block;
	(void) count;
	return EXT2_ET_UNIMPLEMENTED;
}
