CC := gcc
LD := gcc

DEFS := -DVK_HAVE_HIKARU

SDL_CFLAGS := `sdl2-config --cflags`
SDL_LDFLAGS := `sdl2-config --libs`

PKG_CFLAGS := `pkg-config --cflags gl glew jansson`
PKG_LDFLAGS := `pkg-config --libs gl glew jansson`

COMMON_FLAGS = $(DEFS) -I src -I /usr/include/json -Wall -Wno-strict-aliasing -Wno-format -Wno-unused-local-typedefs

CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) $(SDL_CFLAGS) -O3 -fomit-frame-pointer -flto -march=native
#CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) $(SDL_CFLAGS) -O0 -g
LDFLAGS := -lm $(PKG_LDFLAGS) $(SDL_LDFLAGS)

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
	src/vk/renderer.o

SH4_OBJ := \
	src/cpu/sh/sh4.o

HIKARU_OBJ := \
	$(SH4_OBJ) \
	src/mach/hikaru/hikaru.o \
	src/mach/hikaru/hikaru-mscomm.o \
	src/mach/hikaru/hikaru-mie.o \
	src/mach/hikaru/hikaru-memctl.o \
	src/mach/hikaru/hikaru-renderer.o \
	src/mach/hikaru/hikaru-gpu.o \
	src/mach/hikaru/hikaru-gpu-cp.o \
	src/mach/hikaru/hikaru-gpu-private.o \
	src/mach/hikaru/hikaru-aica.o

all: bin/valkyrie bin/vkbswap

bin/valkyrie: $(VK_OBJ) $(HIKARU_OBJ) src/vk/main.o
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

