#
# SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
#
# SPDX-License-Identifier: Apache-2.0
#
COMMON_SRCS=src/main.c src/disk.c src/ui/popup.c src/ui/combo_disk.c src/ui/message_box.c src/ui/menubar.c src/ui/statusbar.c src/ui/partition_viewer.c src/zealfs/zealfs_v2.c src/ui/tinyfiledialogs.c

CC=gcc
CFLAGS=-O2 -g -Wall -Iinclude -Iraylib/linux/include -Lraylib/linux/lib -Wno-format-truncation
LDFLAGS=-lraylib -lm
TARGET=zeal_disk_tool.elf
# Path for linuxdeploy
LINUXDEPLOY?=./linuxdeploy-x86_64.AppImage

all: include/app_version.h $(TARGET)

include/app_version.h: FORCE
	@echo "#define VERSION \"$$(git describe --always || echo local)\"" > $@

FORCE:


##########################
#       Shortcuts        #
##########################

linux: $(TARGET)
linux32: $(TARGET)32
windows: $(WIN_TARGET)
macosx: $(MAC_TARGET)


##########################
# Build the Linux binary #
##########################
$(TARGET): src/disk_linux.c $(COMMON_SRCS) build/raylib-nuklear-linux.o
	$(CC) $(CFLAGS) -o $@ $^  $(LDFLAGS)

# To speed up the recompilation of the linux binary, make sure raylib-nuklear is already as an object file
build/raylib-nuklear-linux.o: src/raylib-nuklear.c
	mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $^


# Create an AppImage for the linux binary
deploy: $(TARGET)
	cp $< appdir/zeal_disk_tool
	$(LINUXDEPLOY) --appdir AppDeploy --executable=appdir/zeal_disk_tool --desktop-file appdir/zeal-disk-tool.desktop --icon-file appdir/zeal-disk-tool.png --output appimage

# Same for 32-bit #
$(TARGET)32: src/disk_linux.c $(COMMON_SRCS) build/raylib-nuklear-linux32.o
	$(CC) -m32 -Lraylib/linux32/lib $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/raylib-nuklear-linux32.o: src/raylib-nuklear.c
	mkdir -p build
	$(CC) -m32 $(CFLAGS) -c -o $@ $^

deploy32: $(TARGET)32
	cp $< appdir/zeal_disk_tool
	$(LINUXDEPLOY) --appdir AppDeploy --executable=appdir/zeal_disk_tool --desktop-file appdir/zeal-disk-tool.desktop --icon-file appdir/zeal-disk-tool.png --output appimage

############################
# Build the Windows binary #
############################
WIN_CC=i686-w64-mingw32-gcc
WIN_WINDRES=i686-w64-mingw32-windres
WIN_CFLAGS=-O2 -Wall -Iinclude -Iraylib/win32/include -Lraylib/win32/lib
WIN_LDFLAGS=-lraylib -lwinmm -lgdi32 -lole32 -static -mwindows
WIN_TARGET=zeal_disk_tool.exe

$(WIN_TARGET): src/disk_win.c $(COMMON_SRCS) appdir/zeal-disk-tool.res build/raylib-nuklear-win.o
	$(WIN_CC) $(WIN_CFLAGS) -o $(WIN_TARGET) $^  $(WIN_LDFLAGS)

# Rule to create the icon that will be embedded the EXE file
appdir/zeal-disk-tool.res: appdir/zeal-disk-tool.rc appdir/zeal-disk-tool.manifest appdir/zeal-disk-tool.ico
	$(WIN_WINDRES) $< -O coff -o $@

appdir/zeal-disk-tool.ico:
	convert appdir/zeal-disk-tool_16.png appdir/zeal-disk-tool_32.png appdir/zeal-disk-tool_48.png appdir/zeal-disk-tool_256.png $@

build/raylib-nuklear-win.o: src/raylib-nuklear.c
	mkdir -p build
	$(WIN_CC) $(CFLAGS) -c -o $@ $^

############################
# Build the MacOS binary   #
############################
MAC_CFLAGS=-O2 -Wall -Werror -Iinclude
MAC_LDFLAGS=-lraylib
MAC_TARGET=zeal_disk_tool.darwin.elf
$(MAC_TARGET): src/disk_mac.c $(COMMON_SRCS) build/raylib-nuklear-darwin.o
	$(CC) $(MAC_CFLAGS) -o $@ $^ $(MAC_LDFLAGS)

build/raylib-nuklear-darwin.o: src/raylib-nuklear.c
	mkdir -p build
	$(CC) $(MAC_CFLAGS) -c -o $@ $^

############################
# Common                   #
############################
clean:
	rm -f $(WIN_TARGET) $(MAC_TARGET) $(TARGET) appdir/zeal-disk-tool.res
	rm -rf build/ $(MAC_TARGET).dSYM
