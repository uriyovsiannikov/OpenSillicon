noinst_LTLIBRARIES = libos.la

AM_CFLAGS = $(DIX_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/os/xtrans

XDMCP_SOURCES = xdmcp.c
STRLCAT_SOURCES = strlcat.c strlcpy.c
XORG_SOURCES = log.c

libos_la_SOURCES = 	\
	WaitFor.c	\
	access.c	\
	auth.c		\
	connection.c	\
	io.c		\
	mitauth.c	\
	oscolor.c	\
	osdep.h		\
	osinit.c	\
	utils.c		\
	xdmauth.c	\
	xstrans.c	\
	xauembed.c	\
	$(XORG_SOURCES)

if XDMCP
libos_la_SOURCES += $(XDMCP_SOURCES)
endif

if NEED_STRLCAT
libos_la_SOURCES += $(STRLCAT_SOURCES)
endif

VENDORED_XTRANS_FILES = \
	xtrans/X11/Xtrans/Xtrans.h \
	xtrans/X11/Xtrans/Xtransint.h \
	xtrans/X11/Xtrans/Xtrans.c \
	xtrans/X11/Xtrans/Xtranssock.c \
	xtrans/X11/Xtrans/Xtransutil.c

EXTRA_DIST = $(XDMCP_SOURCES) $(STRLCAT_SOURCES) $(VENDORED_XTRANS_FILES)
