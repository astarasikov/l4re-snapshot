PKGDIR          ?= ../..
L4DIR           ?= $(PKGDIR)/../..

TARGET          = libl4png.a libl4png.so
LIBS            = $(GCCLIB)
SYSTEMS		= $(SYSTEMS_PLAIN)
REQUIRES_LIBS   = zlib
CONTRIB_INCDIR  = libpng

SRC_C           = pngerror.c pngget.c pngpread.c pngrio.c pngrutil.c \
		  pngwrite.c pngwutil.c png.c pngmem.c pngread.c \
		  pngrtran.c pngset.c pngtrans.c pngwio.c pngwtran.c

PRIVATE_INCDIR  = $(SRC_DIR)
DEFINE		= PNG_USER_MEM_SUPPORTED PNG_CONFIGURE_LIBPNG HAVE_CONFIG_H

vpath %.c $(SRC_DIR)/../dist

include $(L4DIR)/mk/lib.mk
