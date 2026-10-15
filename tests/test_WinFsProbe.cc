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

/* Compare the WParted file system prober with blkid on images created by the
 * real mkfs tools (see make_test_images.sh).
 */

#include "WinFsProbe.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


namespace WParted
{


static std::vector<std::string> split(const std::string& line, char sep)
{
	std::vector<std::string> out;
	std::string field;
	std::istringstream in(line);
	while (std::getline(in, field, sep))
		out.push_back(field);
	while (out.size() < 5)
		out.push_back("");
	return out;
}


TEST(WinFsProbe, MatchesBlkid)
{
	const char* dir = std::getenv("WPARTED_TEST_IMAGES");
	if (! dir)
		GTEST_SKIP() << "WPARTED_TEST_IMAGES not set (run tests/make_test_images.sh)";
	std::ifstream expected(std::string(dir) + "/expected.txt");
	ASSERT_TRUE(expected.good());

	int checked = 0;
	std::string line;
	while (std::getline(expected, line))
	{
		std::vector<std::string> f = split(line, '|');
		const std::string& name = f[0];
		std::ifstream img_file(std::string(dir) + "/" + name, std::ios::binary);
		ASSERT_TRUE(img_file.good()) << name;
		img_file.seekg(0, std::ios::end);
		std::vector<uint8_t> img(static_cast<size_t>(img_file.tellg()));
		img_file.seekg(0);
		img_file.read(reinterpret_cast<char*>(img.data()), static_cast<std::streamsize>(img.size()));

		WinDisk::SectorReader reader = [&img](uint64_t lba, uint32_t count, std::vector<uint8_t>& out)
		{
			if ((lba + count) * 512 > img.size())
				return false;
			out.assign(img.begin() + lba * 512, img.begin() + (lba + count) * 512);
			return true;
		};
		WinFsProbe::Result r;
		ASSERT_TRUE(WinFsProbe::probe(reader, 512, img.size() / 512, r)) << name;
		EXPECT_EQ(f[1], r.type) << name;
		if (r.type == "vfat")
		{
			EXPECT_EQ(f[2], r.sec_type) << name;
		}
		if (r.type != "f2fs")
		{
			EXPECT_EQ(f[3], r.uuid) << name;
		}
		if (r.type != "ntfs" && r.type != "f2fs")
		{
			EXPECT_EQ(f[4], r.label) << name;
		}
		checked++;
	}
	EXPECT_GE(checked, 11);
}


TEST(WinFsProbe, EmptyIsUnknown)
{
	std::vector<uint8_t> img(4 * 1024 * 1024, 0);
	WinDisk::SectorReader reader = [&img](uint64_t lba, uint32_t count, std::vector<uint8_t>& out)
	{
		if ((lba + count) * 512 > img.size())
			return false;
		out.assign(img.begin() + lba * 512, img.begin() + (lba + count) * 512);
		return true;
	};
	WinFsProbe::Result r;
	EXPECT_FALSE(WinFsProbe::probe(reader, 512, img.size() / 512, r));
	EXPECT_TRUE(r.type.empty());
}


TEST(WinFsProbe, LibpartedNames)
{
	WinFsProbe::Result r;
	r.type = "vfat"; r.sec_type = "msdos";
	EXPECT_EQ("fat16", r.libparted_name());
	r.sec_type = "";
	EXPECT_EQ("fat32", r.libparted_name());
	r.type = "swap";
	EXPECT_EQ("linux-swap", r.libparted_name());
	r.type = "BitLocker";
	EXPECT_EQ("", r.libparted_name());  // Unknown to the partition type logic: type kept
	r.type = "crypto_LUKS";
	EXPECT_EQ("", r.libparted_name());
}


}  // namespace WParted
