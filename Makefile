# DAO - build with MinGW-w64 (MSYS2) or cross-compile from Linux:
#   make                                  native MinGW
#   make CROSS=x86_64-w64-mingw32-        cross toolchain
#   make STORE_ID=<chrome-web-store-id>   installer that auto-registers the extension

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres
CFLAGS  ?= -O2 -Wall -Wextra -Wno-missing-field-initializers
CFLAGS  += -municode
BUILD   := build

APP_LIBS   := -lwinhttp -lcomctl32 -lshell32 -lole32 -luxtheme -ldwmapi -lgdi32 -luser32 -luuid -ladvapi32 -lcrypt32
SETUP_LIBS := -lole32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32

ifneq ($(STORE_ID),)
SETUP_DEFS := -DSTORE_EXTENSION_ID='L"$(STORE_ID)"'
endif

APP_SRC := src/main.c src/engine.c src/i18n.c
EXT_FILES := $(wildcard extension/*.* extension/icons/*.png extension/_locales/*/messages.json)

all: $(BUILD)/dao.exe $(BUILD)/dao-host.exe $(BUILD)/dao-cli.exe $(BUILD)/DAO-Setup.exe

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/dao_res.o: res/dao.rc res/dao.ico res/dao.manifest | $(BUILD)
	$(WINDRES) $< -O coff -o $@

$(BUILD)/host_res.o: res/host.rc res/dao.ico | $(BUILD)
	$(WINDRES) $< -O coff -o $@

$(BUILD)/dao.exe: $(APP_SRC) src/engine.h src/i18n.h $(BUILD)/dao_res.o
	$(CC) $(CFLAGS) -mwindows -o $@ $(APP_SRC) $(BUILD)/dao_res.o $(APP_LIBS)

$(BUILD)/dao-host.exe: src/host.c $(BUILD)/host_res.o
	$(CC) $(CFLAGS) -mwindows -o $@ src/host.c $(BUILD)/host_res.o

$(BUILD)/dao-cli.exe: src/cli.c
	$(CC) $(CFLAGS) -o $@ $< -lwinhttp

$(BUILD)/setup_res.o: installer/setup.rc installer/setup.manifest $(BUILD)/dao.exe $(BUILD)/dao-host.exe \
                      $(BUILD)/dao-cli.exe README.md LICENSE $(EXT_FILES)
	$(WINDRES) $< -O coff -o $@

$(BUILD)/DAO-Setup.exe: installer/setup.c installer/payload.h $(BUILD)/setup_res.o
	$(CC) $(CFLAGS) $(SETUP_DEFS) -mwindows -o $@ installer/setup.c $(BUILD)/setup_res.o $(SETUP_LIBS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
