CC := gcc
LD := gcc

DEFS := -DVK_HAVE_HIKARU

PKG_CFLAGS := `pkg-config --cflags gl glew sdl2 jansson`
PKG_LDFLAGS := `pkg-config --libs gl glew sdl2 jansson`

COMMON_FLAGS = $(DEFS) -I src -I /usr/include/json -Wall -Wno-strict-aliasing -Wno-format -Wno-unused-local-typedefs

CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) -O3 -fomit-frame-pointer -flto -march=native
#CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) -O0 -g
LDFLAGS := -lm $(PKG_LDFLAGS)

.PHONY: all install clean

VK_OBJ := \
	src/vk/core.o \
	src/vk/vector.o \
	src/vk/state.o \
	src/vk/buffer.o \
	src/vk/region.o \
	src/vk/mmap.o \
	src/vk/machine.o \
	src/vk/games.o \
	src/vk/input.o \
	src/vk/renderer.o \
	src/vk/surface.o \

SH4_OBJ := \
	src/cpu/sh/sh4.o

HIKARU_OBJ := \
	$(SH4_OBJ) \
	src/mach/hikaru/hikaru.o \
	src/mach/hikaru/hikaru-mscomm.o \
	src/mach/hikaru/hikaru-mie.o \
	src/mach/hikaru/hikaru-memctl.o \
	src/mach/hikaru/hikaru-renderer.o \
	src/mach/hikaru/hikaru-renderer-texture.o \
	src/mach/hikaru/hikaru-gpu.o \
	src/mach/hikaru/hikaru-gpu-cp.o \
	src/mach/hikaru/hikaru-gpu-private.o \
	src/mach/hikaru/hikaru-aica.o

all: bin/valkyrie bin/hikaru-gpu-viewer bin/vkbswap

bin/valkyrie: $(VK_OBJ) $(HIKARU_OBJ) src/vk/main.o
	$(CC) $+ -o $@ $(CFLAGS) $(LDFLAGS) 

bin/hikaru-gpu-viewer: $(VK_OBJ) $(HIKARU_OBJ) src/mach/hikaru/hikaru-gpu-viewer.o
	$(CC) $+ -o $@ $(CFLAGS) $(LDFLAGS)

bin/vkbswap: $(VK_OBJ) src/utils/bswap.o
	$(CC) $+ -o $@ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.c %.h
	$(CC) -c $< -o $@ $(CFLAGS)

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/valkyrie $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/valkyrie )
	@( cp -v vk-games.json $(HOME)/.local/share/valkyrie )

clean:
	find . -name '*.o' | xargs rm -vf
	rm -vf bin/*

