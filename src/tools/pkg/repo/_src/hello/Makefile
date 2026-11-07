# A package recipe exposes its archive file, not an additional public goal.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PACKAGE := src/tools/pkg/repo/_pkges/hello-1.0.0.spkg
.DEFAULT_GOAL := $(ROOT)/$(PACKAGE)
$(ROOT)/$(PACKAGE): $(ROOT)/src/tools/pkg/repo/_src/hello/hello.c
	$(MAKE) -C $(ROOT) $(PACKAGE)
