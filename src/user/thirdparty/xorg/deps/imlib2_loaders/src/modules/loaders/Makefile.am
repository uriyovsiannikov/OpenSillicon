MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS            = $(CFLAGS_WARNINGS) $(CFLAGS_VISIBILITY)

pkgdir               = $(libdir)/imlib2/loaders

pkg_LTLIBRARIES =
if BUILD_EET_LOADER
pkg_LTLIBRARIES += eet.la
endif
if BUILD_XCF_LOADER
pkg_LTLIBRARIES += xcf.la
endif

#if ENABLE_RTLD_LOCAL_SUPPORT
#IMLIB2_LIBS = $(top_builddir)/src/lib/libImlib2.la
#endif

AM_LIBTOOLFLAGS = --tag=disable-static

eet_la_SOURCES      = loader_eet.c
eet_la_CPPFLAGS     = $(EET_CFLAGS)
eet_la_LDFLAGS      = -module -avoid-version
eet_la_LIBADD       = $(IMLIB2_LIBS) $(EET_LIBS)

xcf_la_SOURCES      = loader_xcf.c loader_xcf_pixelfuncs.c loader_xcf.h
xcf_la_LDFLAGS      = -module -avoid-version
xcf_la_LIBADD       = $(IMLIB2_LIBS)
