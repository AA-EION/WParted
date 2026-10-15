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

/* Test harness (Linux): serve a byte range of an image file through the
 * WParted disk broker to one helper program, exactly as WParted does on
 * Windows, and report what the broker refused.
 *
 *   wparted_broker_harness [--ro] IMAGE OFFSET LENGTH -- PROGRAM [ARGS...]
 *
 * Exit status is the helper's.  Broker statistics go to stderr as
 * "broker: read=N written=N refused=N".
 */

#include "WinBroker.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>


namespace
{

class FileSession : public WParted::WinDisk::DiskSession
{
public:
	FileSession(int fd, uint64_t offset, uint64_t length, bool writable)
	 : m_fd(fd), m_offset(offset), m_length(length), m_writable(writable) {}

	uint64_t length() const override       { return m_length; }
	uint32_t sector_size() const override  { return 512; }
	bool writable() const override         { return m_writable; }

	bool read(uint64_t offset, uint64_t length, void* buffer, std::string& error) override
	{
		if (offset > m_length || length > m_length - offset)
			return false;
		ssize_t n = pread(m_fd, buffer, length, static_cast<off_t>(m_offset + offset));
		if (n != static_cast<ssize_t>(length))
		{
			error = std::strerror(errno);
			return false;
		}
		return true;
	}

	bool write(uint64_t offset, uint64_t length, const void* buffer, std::string& error) override
	{
		if (! m_writable || offset > m_length || length > m_length - offset || offset % 512 || length % 512)
			return false;
		ssize_t n = pwrite(m_fd, buffer, length, static_cast<off_t>(m_offset + offset));
		if (n != static_cast<ssize_t>(length))
		{
			error = std::strerror(errno);
			return false;
		}
		return true;
	}

	bool flush(std::string& error) override
	{
		return fdatasync(m_fd) == 0;
	}

private:
	int      m_fd;
	uint64_t m_offset;
	uint64_t m_length;
	bool     m_writable;
};


class FdChannel : public WParted::Broker::Channel
{
public:
	FdChannel(int rd, int wr) : m_rd(rd), m_wr(wr) {}
	bool read_exact(void* buffer, size_t length) override
	{
		uint8_t* p = static_cast<uint8_t*>(buffer);
		while (length)
		{
			ssize_t n = ::read(m_rd, p, length);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				return false;
			p += n;
			length -= static_cast<size_t>(n);
		}
		return true;
	}
	bool write_all(const void* buffer, size_t length) override
	{
		const uint8_t* p = static_cast<const uint8_t*>(buffer);
		while (length)
		{
			ssize_t n = ::write(m_wr, p, length);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				return false;
			p += n;
			length -= static_cast<size_t>(n);
		}
		return true;
	}
private:
	int m_rd, m_wr;
};

}  // anonymous namespace


int main(int argc, char** argv)
{
	int i = 1;
	bool read_only = false;
	if (i < argc && std::strcmp(argv[i], "--ro") == 0)
	{
		read_only = true;
		i++;
	}
	if (argc - i < 5 || std::strcmp(argv[i + 3], "--") != 0)
	{
		std::fprintf(stderr, "usage: %s [--ro] IMAGE OFFSET LENGTH -- PROGRAM [ARGS...]\n", argv[0]);
		return 2;
	}
	const char* image = argv[i];
	uint64_t offset = std::strtoull(argv[i + 1], nullptr, 0);
	uint64_t length = std::strtoull(argv[i + 2], nullptr, 0);
	char** child_argv = &argv[i + 4];

	int fd = open(image, read_only ? O_RDONLY : O_RDWR);
	if (fd < 0)
	{
		std::perror(image);
		return 2;
	}
	FileSession session(fd, offset, length, ! read_only);

	int req[2], rep[2];
	if (pipe(req) != 0 || pipe(rep) != 0)
	{
		std::perror("pipe");
		return 2;
	}
	pid_t pid = fork();
	if (pid == 0)
	{
		close(req[0]);
		close(rep[1]);
		close(fd);
		std::string env = std::to_string(rep[0]) + "," + std::to_string(req[1]);
		setenv("WPARTED_BROKER", env.c_str(), 1);
		execv(child_argv[0], child_argv);
		std::perror(child_argv[0]);
		_exit(127);
	}
	close(req[1]);
	close(rep[0]);

	WParted::Broker::Server server(session);
	std::thread serve([&]()
	{
		FdChannel ch(req[0], rep[1]);
		server.serve(ch);
	});
	int status = 0;
	waitpid(pid, &status, 0);
	close(rep[1]);
	serve.join();
	close(req[0]);
	close(fd);

	const auto& st = server.statistics();
	std::fprintf(stderr, "broker: read=%llu written=%llu refused=%llu\n",
	             static_cast<unsigned long long>(st.bytes_read),
	             static_cast<unsigned long long>(st.bytes_written),
	             static_cast<unsigned long long>(st.refused));
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
