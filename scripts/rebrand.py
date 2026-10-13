import os
import re

PORT_HEADER = """/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
"""

def rebrand_file(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # Prepend WParted header if not present and has copyright
    new_content = content
    if "/* WParted — Native Windows Partition Editor" not in new_content:
        if new_content.startswith("/* Copyright"):
            new_content = PORT_HEADER + new_content[3:]
        elif new_content.startswith("/*"):
            new_content = PORT_HEADER + new_content[3:]

    # Header guards
    new_content = re.sub(r'#ifndef\s+GPARTED_([A-Za-z0-9_]+)', r'#ifndef WPARTED_\1', new_content)
    new_content = re.sub(r'#define\s+GPARTED_([A-Za-z0-9_]+)', r'#define WPARTED_\1', new_content)

    # Includes
    new_content = new_content.replace('"GParted_Core.h"', '"WParted_Core.h"')
    new_content = new_content.replace('"Win_GParted.h"', '"Win_WParted.h"')

    # Namespaces & Classes
    new_content = re.sub(r'\bnamespace\s+GParted\b', 'namespace WParted', new_content)
    new_content = re.sub(r'\bGParted::', 'WParted::', new_content)
    new_content = re.sub(r'\bGParted_Core\b', 'WParted_Core', new_content)
    new_content = re.sub(r'\bWin_GParted\b', 'Win_WParted', new_content)

    # Strings
    new_content = new_content.replace('"GNOME Partition Editor"', '"WParted — Native Windows Partition Editor (Forked from GParted)"')
    new_content = new_content.replace('_("GNOME Partition Editor")', '_("WParted — Native Windows Partition Editor (Forked from GParted)")')
    new_content = new_content.replace('dialog.set_program_name(_("GParted"))', 'dialog.set_program_name(_("WParted"))')
    new_content = new_content.replace('dialog.set_program_name( "GParted" )', 'dialog.set_program_name( "WParted" )')
    new_content = new_content.replace('dialog .set_program_name( "GParted" )', 'dialog .set_program_name( "WParted" )')
    new_content = new_content.replace('dialog .set_logo_icon_name( "gparted" )', 'dialog .set_logo_icon_name( "wparted" )')
    new_content = new_content.replace('dialog.set_logo_icon_name("gparted")', 'dialog.set_logo_icon_name("wparted")')
    new_content = new_content.replace('dialog.set_comments(_("GNOME Partition Editor"))', 'dialog.set_comments(_("WParted — Native Windows Partition Editor (Forked from GParted)"))')
    new_content = new_content.replace('dialog .set_comments( _( "GNOME Partition Editor" ) )', 'dialog .set_comments( _( "WParted — Native Windows Partition Editor (Forked from GParted)" ) )')

    # Domain & prgname in main
    new_content = new_content.replace('Glib::set_prgname("gparted");', 'Glib::set_prgname("wparted");')
    new_content = new_content.replace('gdk_set_program_class( "GParted" ) ;', 'gdk_set_program_class( "WParted" ) ;')
    new_content = new_content.replace('"Root privileges are required for running GParted"', '"Administrator privileges are required for running WParted"')
    new_content = new_content.replace('"Since GParted is a powerful tool capable of destroying partition tables and vast amounts of data, only root may run it."',
                                      '"Since WParted is a powerful tool capable of modifying partition tables and raw storage volumes, Administrator privileges are required."')

    if new_content != content:
        with open(path, "w", encoding="utf-8") as f:
            f.write(new_content)

def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    
    # Rename files
    renames = [
        ("include/GParted_Core.h", "include/WParted_Core.h"),
        ("src/GParted_Core.cc", "src/WParted_Core.cc"),
        ("include/Win_GParted.h", "include/Win_WParted.h"),
        ("src/Win_GParted.cc", "src/Win_WParted.cc"),
        ("gparted.appdata.xml.in", "wparted.appdata.xml.in"),
        ("gparted.desktop.in.in", "wparted.desktop.in.in"),
        ("gparted.doap", "wparted.doap"),
        ("gparted.in", "wparted.in"),
        ("org.gnome.gparted.policy.in.in", "org.gnome.wparted.policy.in.in"),
    ]
    
    for src, dst in renames:
        s_path = os.path.join(root, src)
        d_path = os.path.join(root, dst)
        if os.path.exists(s_path):
            if os.path.exists(d_path):
                os.remove(d_path)
            os.rename(s_path, d_path)
            print(f"Renamed: {src} -> {dst}")

    # Process all header and source files
    target_dirs = ["include", "src", "tests"]
    for td in target_dirs:
        dir_path = os.path.join(root, td)
        for dirpath, _, filenames in os.walk(dir_path):
            for fname in filenames:
                if fname.endswith((".h", ".cc", ".cpp", ".c")):
                    rebrand_file(os.path.join(dirpath, fname))
                    
    # Also rebrand data/icons if present
    icons_dir = os.path.join(root, "data", "icons")
    if os.path.exists(icons_dir):
        for fname in os.listdir(icons_dir):
            if "gparted" in fname:
                new_name = fname.replace("gparted", "wparted")
                os.rename(os.path.join(icons_dir, fname), os.path.join(icons_dir, new_name))
                print(f"Renamed icon: {fname} -> {new_name}")
        svg_dir = os.path.join(icons_dir, "svg")
        if os.path.exists(svg_dir):
            for fname in os.listdir(svg_dir):
                if "gparted" in fname:
                    new_name = fname.replace("gparted", "wparted")
                    os.rename(os.path.join(svg_dir, fname), os.path.join(svg_dir, new_name))
                    print(f"Renamed svg: {fname} -> {new_name}")

if __name__ == "__main__":
    main()
