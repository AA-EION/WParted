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

#include "WinBroker.h"

#include <cstring>


namespace WParted
{
namespace Broker
{


static void put32(uint8_t* p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = static_cast<uint8_t>(v >> (8 * i));
}


static void put64(uint8_t* p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = static_cast<uint8_t>(v >> (8 * i));
}


static uint32_t get32(const uint8_t* p)
{
	uint32_t v = 0;
	for (int i = 3; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}


static uint64_t get64(const uint8_t* p)
{
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}


void encode_request(const Request& r, uint8_t out[REQUEST_SIZE])
{
	put32(out, r.magic);
	put32(out + 4, r.op);
	put64(out + 8, r.offset);
	put32(out + 16, r.length);
	put32(out + 20, r.reserved);
}


bool decode_request(const uint8_t in[REQUEST_SIZE], Request& r)
{
	r.magic    = get32(in);
	r.op       = get32(in + 4);
	r.offset   = get64(in + 8);
	r.length   = get32(in + 16);
	r.reserved = get32(in + 20);
	return r.magic == REQUEST_MAGIC;
}


void encode_reply(const Reply& r, uint8_t out[REPLY_SIZE])
{
	put32(out, r.magic);
	put32(out + 4, static_cast<uint32_t>(r.status));
	put32(out + 8, r.length);
	put32(out + 12, r.reserved);
}


bool decode_reply(const uint8_t in[REPLY_SIZE], Reply& r)
{
	r.magic    = get32(in);
	r.status   = static_cast<int32_t>(get32(in + 4));
	r.length   = get32(in + 8);
	r.reserved = get32(in + 12);
	return r.magic == REPLY_MAGIC;
}


Server::Server(WinDisk::DiskSession& session)
 : m_session(session)
{
}


bool Server::handle(const Request& req, const uint8_t* data, Reply& reply, std::vector<uint8_t>& out)
{
	reply = Reply();
	out.clear();
	m_stats.requests++;

	if (req.magic != REQUEST_MAGIC)
	{
		reply.status = STATUS_PROTOCOL;
		m_last_error = "bad request magic";
		return false;
	}

	auto refuse = [&](int32_t status, const char* why)
	{
		reply.status = status;
		m_stats.refused++;
		m_last_error = why;
		return true;   // The conversation may continue
	};

	switch (req.op)
	{
		case OP_INFO:
		{
			out.resize(INFO_SIZE);
			put64(out.data(), m_session.length());
			put32(out.data() + 8, m_session.sector_size());
			put32(out.data() + 12, m_session.writable() ? 0 : INFO_FLAG_READ_ONLY);
			reply.length = INFO_SIZE;
			return true;
		}

		case OP_READ:
		case OP_WRITE:
		{
			if (req.length == 0 || req.length > MAX_TRANSFER)
				return refuse(STATUS_TOO_BIG, "transfer length out of range");
			if (req.offset > m_session.length() || req.length > m_session.length() - req.offset)
				return refuse(STATUS_RANGE, "access outside of the partition");

			if (req.op == OP_READ)
			{
				out.resize(req.length);
				std::string error;
				if (! m_session.read(req.offset, req.length, out.data(), error))
				{
					out.clear();
					return refuse(STATUS_IO, "read failed");
				}
				reply.length = req.length;
				m_stats.bytes_read += req.length;
				return true;
			}

			if (! m_session.writable())
				return refuse(STATUS_READ_ONLY, "write on a read-only session");
			const uint32_t ss = m_session.sector_size();
			if (ss == 0 || req.offset % ss != 0 || req.length % ss != 0)
				return refuse(STATUS_ALIGNMENT, "unaligned write");
			std::string error;
			if (! m_session.write(req.offset, req.length, data, error))
				return refuse(STATUS_IO, "write failed");
			m_stats.bytes_written += req.length;
			return true;
		}

		case OP_FLUSH:
		{
			std::string error;
			if (! m_session.flush(error))
				return refuse(STATUS_IO, "flush failed");
			return true;
		}

		case OP_CLOSE:
			return false;

		default:
			reply.status = STATUS_PROTOCOL;
			m_last_error = "unknown operation";
			return false;
	}
}


bool Server::serve(Channel& channel)
{
	std::vector<uint8_t> data;
	std::vector<uint8_t> out;
	for (;;)
	{
		uint8_t head[REQUEST_SIZE];
		if (! channel.read_exact(head, REQUEST_SIZE))
		{
			m_last_error = "helper closed the connection";
			return false;
		}
		Request req;
		decode_request(head, req);

		data.clear();
		if (req.magic == REQUEST_MAGIC && req.op == OP_WRITE)
		{
			// Payload must be consumed to keep the stream in step, but never more
			// than the maximum transfer: a larger length is a protocol error.
			if (req.length > MAX_TRANSFER)
			{
				m_last_error = "write length above the maximum";
				return false;
			}
			data.resize(req.length);
			if (req.length && ! channel.read_exact(data.data(), req.length))
			{
				m_last_error = "helper closed the connection";
				return false;
			}
		}

		Reply reply;
		bool more = handle(req, data.data(), reply, out);
		uint8_t rhead[REPLY_SIZE];
		encode_reply(reply, rhead);
		if (! channel.write_all(rhead, REPLY_SIZE) ||
		    (! out.empty() && ! channel.write_all(out.data(), out.size())))
		{
			m_last_error = "unable to reply to the helper";
			return false;
		}
		if (! more)
			return req.magic == REQUEST_MAGIC && req.op == OP_CLOSE;
	}
}


}  // namespace Broker
}  // namespace WParted
