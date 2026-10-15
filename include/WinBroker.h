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

/* WinBroker
 *
 * Disk access for sandboxed helper programs (mke2fs, e2fsck, the Apple file
 * system reader, ...).  Helpers run without administrator rights and can not
 * open disks.  Instead they send requests over a pipe and WParted serves them
 * from a DiskSession limited to one partition, read-only when the operation
 * does not need to write.
 *
 * The wire format is shared with the C client in helpers/broker/wpbroker.h.
 * All integers are little-endian.
 *
 *     request : u32 magic | u32 op | u64 offset | u32 length | u32 reserved
 *               [length bytes of data for WRITE]
 *     reply   : u32 magic | i32 status | u32 length | u32 reserved
 *               [length bytes of data for INFO and READ]
 *
 * Offsets are relative to the start of the partition.  Reads may be
 * unaligned, writes must cover whole sectors.
 */

#ifndef WPARTED_WINBROKER_H
#define WPARTED_WINBROKER_H

#include "WinDiskBackend.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace WParted
{
namespace Broker
{


const uint32_t REQUEST_MAGIC = 0x52425057;   // "WPBR"
const uint32_t REPLY_MAGIC   = 0x41425057;   // "WPBA"
const uint32_t MAX_TRANSFER  = 16u * 1024 * 1024;

enum Op : uint32_t
{
	OP_INFO  = 1,
	OP_READ  = 2,
	OP_WRITE = 3,
	OP_FLUSH = 4,
	OP_CLOSE = 5,
};

enum Status : int32_t
{
	STATUS_OK        =  0,
	STATUS_PROTOCOL  = -1,   // Bad magic, unknown operation
	STATUS_RANGE     = -2,   // Outside of the partition
	STATUS_READ_ONLY = -3,   // Write on a read-only session
	STATUS_IO        = -4,   // Disk error
	STATUS_ALIGNMENT = -5,   // Write not on sector boundaries
	STATUS_TOO_BIG   = -6,   // Length above MAX_TRANSFER
};

const uint32_t INFO_FLAG_READ_ONLY = 1;

struct Request
{
	uint32_t magic    = REQUEST_MAGIC;
	uint32_t op       = 0;
	uint64_t offset   = 0;
	uint32_t length   = 0;
	uint32_t reserved = 0;
};
const size_t REQUEST_SIZE = 24;

struct Reply
{
	uint32_t magic    = REPLY_MAGIC;
	int32_t  status   = STATUS_OK;
	uint32_t length   = 0;
	uint32_t reserved = 0;
};
const size_t REPLY_SIZE = 16;

// INFO reply payload: u64 size | u32 sector size | u32 flags
const size_t INFO_SIZE = 16;

void encode_request(const Request& r, uint8_t out[REQUEST_SIZE]);
bool decode_request(const uint8_t in[REQUEST_SIZE], Request& r);
void encode_reply(const Reply& r, uint8_t out[REPLY_SIZE]);
bool decode_reply(const uint8_t in[REPLY_SIZE], Reply& r);


// Byte stream between WParted and one helper.
class Channel
{
public:
	virtual ~Channel() {}
	// Read exactly 'length' bytes.  False on end of stream or error.
	virtual bool read_exact(void* buffer, size_t length) = 0;
	virtual bool write_all(const void* buffer, size_t length) = 0;
};


struct Statistics
{
	uint64_t requests      = 0;
	uint64_t bytes_read    = 0;
	uint64_t bytes_written = 0;
	uint64_t refused       = 0;
};


class Server
{
public:
	// 'session' must outlive the server.
	explicit Server(WinDisk::DiskSession& session);

	// Handle one request.  'data' holds 'req.length' bytes for OP_WRITE.  Fills
	// the reply and, for INFO and READ, 'out'.  Returns false when the
	// conversation must end (a protocol error).  OP_CLOSE only marks the
	// conversation as finished; serving continues until end of stream so a
	// helper can never block itself by talking after closing.
	bool handle(const Request& req, const uint8_t* data, Reply& reply, std::vector<uint8_t>& out);

	// Serve requests until end of stream or a protocol error.  Returns true
	// when the stream ended after an OP_CLOSE.
	bool serve(Channel& channel);

	const Statistics& statistics() const  { return m_stats; }
	const std::string& last_error() const  { return m_last_error; }

private:
	WinDisk::DiskSession& m_session;
	Statistics            m_stats;
	std::string           m_last_error;
	bool                  m_closed = false;
};


}  // namespace Broker
}  // namespace WParted

#endif /* WPARTED_WINBROKER_H */
