# discontinued for now........
# avocadOS

A lightweight 32-bit desktop built on [ReactOS](https://reactos.org/). It remasters the ReactOS install disc and adds:

- the avocadOS shell: a full-screen tile launcher with a taskbar, clock and switchable wallpapers
- Firefox 52 ESR, VLC 2.2.8, LibreOffice 5.4 (Writer, Calc, Impress, Draw, Base, Math) and Python 3.4
- a Dev Tools page with install links and compatibility notes for C/C++, Java and Rust

The ISO is built by GitHub Actions, not on a developer machine, so every build is downloaded fresh and boot-tested in QEMU.

## Getting the ISO

1. Open the **Actions** tab and pick the latest successful **build-iso** run (or run it with *Run workflow*).
2. Download the `avocadOS-iso` artifact and unzip it to get `avocadOS.iso`.
3. Tick *publish_release* when you run the workflow to get the ISO as a normal release download instead.

## Installing

1. Boot the ISO in VirtualBox or QEMU first (give it 1 GB of RAM) before trying real hardware.
2. Install ReactOS as usual.
3. Open a command prompt and run `D:\avocados\first-boot.cmd` (use your CD drive letter). It copies the shell, installs the apps silently and makes avocadOS the default desktop.
4. Restart.

## What to expect

ReactOS is alpha software that implements only part of Windows, so:

- Old app versions are used on purpose. Current Firefox, LibreOffice and VLC need Windows APIs ReactOS does not have.
- Any of the apps may crash or only partly work. The build checks that the disc boots, not that every app runs.
- Wi-Fi support in ReactOS is very limited. Ethernet and PS/2 or USB keyboards and mice generally work.
- Rust does not run on ReactOS at all (current Rust dropped XP-era Windows).
- 3D graphics acceleration is not supported. Plain VGA/VBE drivers are used.

## Layout

| Path | What it is |
| --- | --- |
| `shell/avocado-shell.c` | the launcher and taskbar (Win32 + GDI) |
| `kit/` | tile list, first-boot script, dev tools page, source URLs |
| `tools/make_wallpapers.py` | generates the wallpapers |
| `scripts/` | download, compile, remaster the ISO, QEMU boot test |
| `.github/workflows/build-iso.yml` | the build pipeline |

To change what the launcher shows, edit `kit/apps.ini`. To change app versions, edit `kit/sources.env`.

## Licenses

ReactOS and the bundled apps are unmodified upstream downloads under their own licenses (GPL, MPL, LGPL, PSF). The avocadOS code in this repository is MIT licensed.
