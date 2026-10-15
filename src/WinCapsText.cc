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

#if defined(_WIN32)

#include "WinCapsText.h"
#include "WinComponents.h"
#include "i18n.h"


namespace WParted
{
namespace WinCapsText
{


namespace
{

Caps::Summary summary_of(FSType fstype, const Caps::DiskContext& disk)
{
	return Caps::summarise(Utils::get_filesystem_string(fstype).raw(), disk, WinComponents::current());
}

}  // anonymous namespace


Glib::ustring ops(const std::vector<Caps::Op>& list)
{
	Glib::ustring s;
	for (Caps::Op op : list)
	{
		if (! s.empty())
			s += ", ";
		s += _(Caps::op_name(op));
	}
	return s;
}


Glib::ustring explain(FSType fstype, const Caps::DiskContext& disk)
{
	Caps::Summary s = summary_of(fstype, disk);
	if (! s.recognised)
		return "";
	Glib::ustring text;
	auto line = [&text](const Glib::ustring& l)
	{
		if (! text.empty())
			text += "\n";
		text += l;
	};
	/* TO TRANSLATORS: looks like   Done by Windows: Create, Check, Label */
	if (! s.by_windows.empty())
		line(Glib::ustring::compose(_("Done by Windows: %1"), ops(s.by_windows)));
	/* TO TRANSLATORS: looks like   Done by WParted: Read usage, Create */
	if (! s.by_wparted.empty())
		line(Glib::ustring::compose(_("Done by WParted: %1"), ops(s.by_wparted)));
	if (! s.by_wsl.empty())
		line(Glib::ustring::compose(_("Done through WSL (experimental): %1"), ops(s.by_wsl)));
	for (const Caps::Summary::Group& g : s.unavailable)
	{
		/* TO TRANSLATORS: looks like   Not available (Move, Copy): <reason> */
		Glib::ustring l = Glib::ustring::compose(_("Not available (%1): %2"), ops(g.ops), _(g.reason));
		if (! g.detail.empty())
			l += " (" + g.detail + ")";
		if (*g.fix)
			l += " " + Glib::ustring(_(g.fix));
		line(l);
	}
	return text;
}


Glib::ustring providers(FSType fstype)
{
	Caps::Summary s = summary_of(fstype, Caps::DiskContext());
	if (s.read_only)
		return _("WParted, read-only");
	Glib::ustring p;
	auto add = [&p](const Glib::ustring& name)
	{
		if (! p.empty())
			p += ", ";
		p += name;
	};
	if (! s.by_windows.empty())
		add("Windows");
	if (! s.by_wparted.empty())
		add("WParted");
	if (! s.by_wsl.empty())
		add(_("WSL (experimental)"));
	return p.empty() ? Glib::ustring("—") : p;
}


Glib::ustring components()
{
	Caps::Components c = WinComponents::current();
	Glib::ustring text;
	text += c.wparted_tools
	        ? _("WParted's Linux file system tools (e2fsprogs): installed")
	        : _("WParted's Linux file system tools (e2fsprogs): missing, reinstall WParted");
	text += "\n";
	text += c.file_reader
	        ? _("WParted's macOS file reader: installed")
	        : _("WParted's macOS file reader: not part of this version");
	text += "\n";
	text += _("WSL: not used by this version of WParted");
	text += "\n";
	text += _("Drivers: WParted never installs drivers.  It needs none for the operations shown here.");
	return text;
}


}  // namespace WinCapsText
}  // namespace WParted

#endif /* _WIN32 */
