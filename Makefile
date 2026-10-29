# Builds Diary.exe for Windows 95/98 with the MinGW-w64 cross-compiler:
#   make          build/Diary.exe
#   make run      start it in Wine
#
# Diary links no C runtime (see crt.c), only the DLLs every Windows 95/98
# has, and is marked as a Windows 4.0 program so that Windows 95/98 load it.

CC      = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
DEFINES = -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_WINDOWS=0x0410 -D_WIN32_IE=0x0400
CFLAGS  = -Os -Wall -Wextra -Wno-unused-parameter -std=gnu99 $(DEFINES) \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
          -fno-tree-loop-distribute-patterns -mno-stack-arg-probe -march=i386 -mtune=pentium
LDFLAGS = -nostdlib -s -Wl,-e,_Start@0 -Wl,--subsystem,windows:4.0 \
          -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
          -Wl,--disable-dynamicbase,--disable-nxcompat,--disable-reloc-section
LIBS    = -lkernel32 -luser32 -lgdi32 -lcomdlg32 -lwsock32 -lgcc

OBJECTS = build/diary.o build/net.o build/crt.o build/resources.o

all: build/Diary.exe

build/Diary.exe: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

build/%.o: %.c crt.h net.h resource.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/resources.o: diary.rc resource.h build/Diary.ico | build
	$(WINDRES) $(DEFINES) -o $@ diary.rc

build/Diary.ico: tools/make_ico.py | build
	python3 tools/make_ico.py $@

build:
	mkdir -p build

run: build/Diary.exe
	wine build/Diary.exe

clean:
	rm -rf build

.PHONY: all run clean
