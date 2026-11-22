#!/usr/bin/env python3
"""Add a first-logon hook to ReactOS's registry.inf (UTF-16) so the installed system runs
\\avocados\\first-boot.cmd from the install disc automatically.

usage: patch-registry.py registry.inf registry.patched.inf
The CD drive letter is not known at install time, so one RunOnce value per likely letter is added;
each only runs the script if it exists on that drive.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, "rb").read().decode("utf-16")  # the BOM is consumed here

lines = ["", "; avocadOS: run first-boot.cmd from the install disc at first logon"]
for letter in "DEFGH":
    cmd = "cmd.exe /c if exist {0}:\\avocados\\first-boot.cmd {0}:\\avocados\\first-boot.cmd".format(letter)
    lines.append('HKLM,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce","avocadOS-{0}",0x00000000,"{1}"'.format(letter, cmd))
block = "\r\n".join(lines) + "\r\n"

idx = text.find("[AddReg]")
if idx < 0:
    sys.exit("registry.inf has no [AddReg] section")
eol = text.find("\n", idx) + 1
patched = text[:eol] + block + text[eol:]
open(dst, "wb").write(b"\xff\xfe" + patched.encode("utf-16-le"))
print("patched", dst, "(+%d lines)" % len(lines))
