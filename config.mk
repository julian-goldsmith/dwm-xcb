# dwm version
VERSION = 5.8.2
DEBUG = 0

# Customize below to fit your system

# compiler and linker
CC = gcc

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

PKGLIST = xcb-aux xcb-ewmh xcb-icccm xcb-keysyms xcb xdmcp xau

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\" 
CFLAGS = -std=c11 `pkg-config --cflags ${PKGLIST}` ${CPPFLAGS}
LDFLAGS = `pkg-config --libs ${PKGLIST}` -lpthread -lc

ifeq ($(strip $(DEBUG)),1)
  CFLAGS += -g3 -pedantic -Wall -O0 -DDEBUG
  LDFLAGS += -g
else
  CFLAGS += -Os -flto -fuse-linker-plugin
  LDFLAGS += -static -flto -fuse-linker-plugin -s -Xlinker --gc-sections -Os
endif

