AM_CPPFLAGS = \
	@KDRIVE_INCS@ \
	@KDRIVE_CFLAGS@ \
	-I$(abs_top_srcdir)/../../include

noinst_LIBRARIES = libopensillicon.a
bin_PROGRAMS = Xopensillicon

libopensillicon_a_SOURCES = opensillicon.c opensillicon.h
Xopensillicon_SOURCES = opensilliconinit.c
Xopensillicon_LDADD = @OPEN_SILLICON_CRT0@ libopensillicon.a \
	@KDRIVE_LIBS@ @XSERVER_LIBS@
Xopensillicon_LDFLAGS = @OPEN_SILLICON_LDFLAGS@
Xopensillicon_DEPENDENCIES = @OPEN_SILLICON_CRT0@ libopensillicon.a @KDRIVE_LIBS@
