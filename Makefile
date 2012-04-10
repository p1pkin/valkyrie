CC := gcc-4.7
LD := gcc-4.7

DEFS := -DVK_HAVE_HIKARU

CFLAGS  := $(DEFS) -I src -I /usr/include/json -O3 -fomit-frame-pointer -flto -Wall
#CFLAGS  := $(DEFS) -I src -I /usr/include/json -O0 -g -Wall
LDFLAGS := -lm -lSDL -lGLEW -ljansson

.PHONY: all install clean

VK_OBJ := \
	src/vk/core.o \
	src/vk/buffer.o \
	src/vk/region.o \
	src/vk/mmap.o \
	src/vk/games.o \
	src/vk/input.o \
	src/vk/renderer.o \
	src/vk/surface.o \
	src/vk/main.o

SH4_OBJ := \
	src/cpu/sh/sh4.c

HIKARU_OBJ := \
	$(SH4_OBJ) \
	src/mach/hikaru/hikaru.o \
	src/mach/hikaru/hikaru-mscomm.o \
	src/mach/hikaru/hikaru-mie.o \
	src/mach/hikaru/hikaru-memctl.o \
	src/mach/hikaru/hikaru-renderer.o \
	src/mach/hikaru/hikaru-gpu.o \
	src/mach/hikaru/hikaru-gpu-insns.o \
	src/mach/hikaru/hikaru-aica.o

all: bin/valkyrie

bin/valkyrie: $(VK_OBJ) $(HIKARU_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $+ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/valkyrie $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/valkyrie )
	@( cp -v vk-games.json $(HOME)/.local/share/valkyrie )

clean:
	find . -name '*.o' | xargs rm -vf
	rm -vf bin/*

