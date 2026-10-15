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

/* Test helper for tests/test_WinIntegration.cc.  Run inside the WParted
 * sandbox, it tries what a compromised helper would try and reports each
 * result on its own line:
 *
 *   disk=<denied|OPENED> <error>        open \\.\PhysicalDrive0 for reading
 *   volume=<denied|OPENED> <error>      open \\.\C: for reading
 *   file=<denied|CREATED> <error>       create a file in C:\Users\Public
 *   broker=<none|ok size=N ss=N ro=N>
 *   read0=<ok|fail> <first byte>
 *   write0=<ok|fail> <status>           only with the argument "write"
 *   write_past_end=<refused|WRITTEN>
 *   read_past_end=<refused|READ>
 */

#include "wpbroker.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


static void try_open(const char* what, const wchar_t* path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		printf("%s=denied %lu\n", what, (unsigned long)GetLastError());
	else
	{
		printf("%s=OPENED\n", what);
		CloseHandle(h);
	}
}


int main(int argc, char** argv)
{
	int do_write = argc > 1 && strcmp(argv[1], "write") == 0;
	wpb_client* c;
	unsigned char buf[4096];
	HANDLE f;

	try_open("disk", L"\\\\.\\PhysicalDrive0");
	try_open("volume", L"\\\\.\\C:");

	f = CreateFileW(L"C:\\Users\\Public\\wparted-sandbox-probe.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (f == INVALID_HANDLE_VALUE)
		printf("file=denied %lu\n", (unsigned long)GetLastError());
	else
	{
		printf("file=CREATED\n");
		CloseHandle(f);
	}

	c = wpb_connect_env();
	if (! c)
	{
		printf("broker=none\n");
		return 0;
	}
	printf("broker=ok size=%llu ss=%u ro=%d\n", (unsigned long long)wpb_size(c), wpb_sector_size(c),
	       wpb_read_only(c));

	if (wpb_read(c, 0, buf, 512) == 0)
		printf("read0=ok %u\n", buf[0]);
	else
		printf("read0=fail\n");

	if (do_write)
	{
		int st;
		memset(buf, 0x5A, sizeof(buf));
		memcpy(buf, "WPARTED-SANDBOX", 15);
		st = wpb_write(c, 0, buf, 4096);
		printf("write0=%s %d\n", st == 0 ? "ok" : "fail", st);
	}

	/* Bypass the client library checks: the broker in WParted must refuse. */
	printf("write_past_end=%s\n",
	       wpb_unchecked_request(c, 3, wpb_size(c), 512, buf, NULL, 0) == 0 ? "WRITTEN" : "refused");
	printf("read_past_end=%s\n",
	       wpb_unchecked_request(c, 2, wpb_size(c) - 256, 512, NULL, buf, 512) == 0 ? "READ" : "refused");
	printf("write_huge=%s\n",
	       wpb_unchecked_request(c, 3, 0xFFFFFFFFFFFFFE00ULL, 512, buf, NULL, 0) == 0 ? "WRITTEN" : "refused");

	wpb_flush(c);
	wpb_close(c);
	fflush(stdout);
	return 0;
}
