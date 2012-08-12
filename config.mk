# dwm version
VERSION = 5.8.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

PKGLIST = xcb-aux xcb-ewmh xcb-icccm xcb-keysyms xcb
#Xinerama
#PKGLIST += xcb-xinerama

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\" 
CFLAGS = -g3 -std=c99 -pedantic -Wall -O0 ${CPPFLAGS}
#CFLAGS = -Os -std=c99 ${CPPFLAGS}
#CFLAGS = -std=c99 -pedantic -Wall -O3 ${CPPFLAGS}
CFLAGS += `pkg-config --cflags ${PKGLIST}`
#CFLAGS +=  -DDMALLOC

LDFLAGS = `pkg-config --libs ${PKGLIST}` -g
#LDFLAGS = `pkg-config --libs ${PKGLIST}`

# compiler and linker
CC = gcc

