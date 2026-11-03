AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src

CLEANFILES = $(PACKAGE).spec

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 compile \
		       config.guess config.h.in config.sub \
		       configure depcomp install-sh \
		       ltmain.sh missing \
		       m4/*.m4

EXTRA_DIST = AUTHORS COPYING COPYING-PLAIN COPYING.loader_xcf \
	README.in README \
	$(PACKAGE).spec.in


dist-hook: $(top_builddir)/$(PACKAGE).spec

RPR_DEV = `echo -n "1.%(date '+%y%m%d').git"; git rev-parse --short=8 HEAD`
RPR_REL = 1
SED_DEV = "s/\@PACKAGE\@/@PACKAGE@/;s/\@VERSION\@/@VERSION@/;s/\@RPM_RELEASE\@/$(RPR_DEV)/"
SED_REL = "s/\@PACKAGE\@/@PACKAGE@/;s/\@VERSION\@/@VERSION@/;s/\@RPM_RELEASE\@/$(RPR_REL)/"

$(top_builddir)/$(PACKAGE).spec: FORCE
	@if [ -f $(top_srcdir)/.git-src -a -z "$(release)" ]; then \
	  sed $(SED_DEV) $(top_srcdir)/$(PACKAGE).spec.in > $@; \
	else \
	  sed $(SED_REL) $(top_srcdir)/$(PACKAGE).spec.in > $@; \
	fi

.PHONY: release
release:
	$(MAKE) dist release=y

FORCE:
