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

#include "WinDevNames.h"

#include <climits>


namespace WParted
{
namespace WinDevNames
{


static const char DISK_PREFIX[] = "/dev/sd";
static const char PHYSICAL_PREFIX[] = "\\\\.\\PhysicalDrive";
static const int  MAX_DISK_INDEX = 100000;


std::string disk_path(int disk_index)
{
	if (disk_index < 0)
		return "";

	// Bijective base 26, like Linux sd naming: a..z, aa..az, ba..zz, aaa...
	std::string letters;
	long n = static_cast<long>(disk_index) + 1;
	while (n > 0)
	{
		n -= 1;
		letters.insert(letters.begin(), static_cast<char>('a' + (n % 26)));
		n /= 26;
	}
	return std::string(DISK_PREFIX) + letters;
}


static int letters_to_index(const std::string& letters)
{
	if (letters.empty() || letters.size() > 4)
		return -1;
	long n = 0;
	for (char c : letters)
	{
		if (c < 'a' || c > 'z')
			return -1;
		n = n * 26 + (c - 'a' + 1);
	}
	n -= 1;
	if (n < 0 || n > MAX_DISK_INDEX)
		return -1;
	return static_cast<int>(n);
}


static int digits_to_int(const std::string& digits)
{
	if (digits.empty() || digits.size() > 6)
		return -1;
	if (digits.size() > 1 && digits[0] == '0')
		return -1;
	long n = 0;
	for (char c : digits)
	{
		if (c < '0' || c > '9')
			return -1;
		n = n * 10 + (c - '0');
	}
	if (n > MAX_DISK_INDEX)
		return -1;
	return static_cast<int>(n);
}


int disk_index(const std::string& path)
{
	const std::string disk_prefix(DISK_PREFIX);
	if (path.compare(0, disk_prefix.size(), disk_prefix) == 0)
		return letters_to_index(path.substr(disk_prefix.size()));

	// Case insensitive match of \\.\PhysicalDrive
	const std::string phys_prefix(PHYSICAL_PREFIX);
	if (path.size() > phys_prefix.size())
	{
		bool match = true;
		for (size_t i = 0; i < phys_prefix.size() && match; i++)
		{
			char a = path[i];
			char b = phys_prefix[i];
			if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
			match = (a == b);
		}
		if (match)
			return digits_to_int(path.substr(phys_prefix.size()));
	}
	return -1;
}


std::string partition_path(const std::string& disk_path, int partition_number)
{
	return disk_path + std::to_string(partition_number);
}


bool parse_partition_path(const std::string& path, int& disk_idx, int& partition_number)
{
	const std::string disk_prefix(DISK_PREFIX);
	if (path.compare(0, disk_prefix.size(), disk_prefix) != 0)
		return false;

	size_t i = disk_prefix.size();
	size_t letters_start = i;
	while (i < path.size() && path[i] >= 'a' && path[i] <= 'z')
		i++;
	std::string letters = path.substr(letters_start, i - letters_start);
	std::string digits = path.substr(i);

	int d = letters_to_index(letters);
	int p = digits_to_int(digits);
	if (d < 0 || p <= 0)
		return false;
	disk_idx = d;
	partition_number = p;
	return true;
}


std::string physical_drive_path(int disk_index)
{
	return std::string(PHYSICAL_PREFIX) + std::to_string(disk_index);
}


}  // namespace WinDevNames
}  // namespace WParted
