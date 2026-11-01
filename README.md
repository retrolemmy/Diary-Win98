# Diary for Windows 98

A simple one-page-per-day journal for Windows 98, which also runs on
Windows 11 (and should run on Windows 95 with Internet Explorer 4, ME, 2000,
XP and later too). It's the sibling of the
[classic Mac OS](https://github.com/retrolemmy/Diary) (68k Macs, and PowerPC
Macs up to a G4 with Mac OS 9.2.2),
[AmigaOS 3.2](https://github.com/retrolemmy/Diary-Amiga) (tested on an A4000),
[MorphOS/MUI](https://github.com/retrolemmy/Diary-Amiga-MUI) (tested on a Mac mini G4 and
the A4000) and
[Mac OS X](https://github.com/retrolemmy/Diary-MacOSX) versions, and like
them it can keep its entries on a
[Diary Server](https://github.com/retrolemmy/Diary-Server), so all your
computers share one diary.

- One window: Windows' month calendar and a list of all entries (newest
  first) on the left, the selected day on the right with `<`, `Today` and
  `>` buttons
- In the calendar, days with an entry are **bold**. Click a day (or a list
  entry) to open it
- **Find** (Ctrl+F) searches every entry (ignoring case) and narrows the
  list to the matches; **Find Next** (F3) jumps to the next match, across
  entries; **Show All Entries** clears the search
- **Go** menu: Previous Day (Ctrl+PgUp), Next Day (Ctrl+PgDn), Today (Ctrl+T)
- **Format → Font...** sets the entry text's font; it's remembered in
  `Diary.ini` next to the program
- The current entry is saved automatically when you switch days or quit;
  Ctrl+S saves at any time
- Entries are plain text files named `YYYY-MM-DD` (Windows-1252) in a
  **Diary Entries** folder next to the program, so they open in Notepad too.
  Entries from the classic Mac, Amiga and Mac OS X versions can be copied in
  and read as they are. Clearing an entry completely deletes its file
- **File → Diary Server...** keeps the entries on a Diary Server instead
  (by name or IP address, optionally with `:port`, and the server's key if
  it has one). Search then runs on the server, and if an entry was changed
  on another computer since you opened it, Diary asks before saving over it
- Up to 60,000 characters per day (the limit of the Windows 95/98 text field)

Diary is a single `Diary.exe` with nothing to install. It needs the month
calendar that comes with Windows 98 (Windows 95 gets it with Internet
Explorer 4 or later); without it, Diary says so instead of failing.

On Windows 11, keep Diary in a folder of your own (in Documents, say)
rather than in `C:\Program Files`: Diary keeps its entries and settings
next to the program, and Windows doesn't let programs write there. As the
program isn't signed, SmartScreen may warn about it on the first start
("More info", then "Run anyway").

## Diary Server

To share the entries between your computers, run
[Diary Server](https://github.com/retrolemmy/Diary-Server) on a machine in
your network, such as a RetroNAS or any small Linux box with Python 3.9 or
later: download `diary_server.py` from its
[latest release](https://github.com/retrolemmy/Diary-Server/releases/latest)
and start it as its README describes. Diary works with Diary Server 1.0 or
later. The same server also gives modern browsers a Diary web page.

## Building

On Linux, with the [MinGW-w64](https://www.mingw-w64.org) cross-compiler
(`mingw-w64-gcc` on Arch, `gcc-mingw-w64-i686` on Debian) and Python 3:

```sh
make
```

The program is `build/Diary.exe`. `make run` starts it in Wine.

Current MinGW runtimes call functions that Windows 95/98 don't have, so
Diary doesn't use the C runtime at all: it starts at its own entry point,
brings the few C library functions it needs along (`crt.c`), and imports
only Windows' own DLLs. The Makefile also marks it as a Windows 4.0
program, which Windows 95/98 require.

## Icon

The icon is drawn by `tools/make_ico.py`, from the same pixel art as the
classic Mac version's icon; the build runs it.

## Diary on all systems

Diary exists for these systems; the current versions share their entries
through a [Diary Server](https://github.com/retrolemmy/Diary-Server), which
also gives modern browsers a Diary web page:

- [Classic Mac OS](https://github.com/retrolemmy/Diary) for 68k and PowerPC
  Macs (tested on a Mac with Mac OS 8.1 and a Power Mac G4 with Mac OS 9.2.2)
- [AmigaOS 3.2](https://github.com/retrolemmy/Diary-Amiga) (tested on an A4000)
- [MorphOS and AmigaOS with MUI](https://github.com/retrolemmy/Diary-Amiga-MUI)
  (tested on a Mac mini G4 with MorphOS and on the A4000)
- [Mac OS X 10.4 and 10.5](https://github.com/retrolemmy/Diary-MacOSX) for
  PowerPC and Intel Macs (tested on Leopard)
- [Windows 98](https://github.com/retrolemmy/Diary-Win98) (tested on Windows 98
  and Windows 11)
- [Diary Server](https://github.com/retrolemmy/Diary-Server) (tested on a
  RetroNAS)

## License

No rights reserved: Diary is dedicated to the public domain under
[CC0 1.0](LICENSE). Use it for anything, no attribution needed.
