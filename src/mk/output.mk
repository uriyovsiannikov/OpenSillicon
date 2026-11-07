# Keep the original shell and recipe boundaries. Only presentation changes:
# every command still executes in its own /bin/sh, including recursive make.
V ?= 0
BUILD_SPINNER ?= 1
BUILD_STATUS_SCRIPT ?= $(abspath src/tools/build_status.py)
BUILD_LOG_DIR ?= $(abspath $(BUILD_DIR)/logs)

ifneq ($(V),1)
ifndef BUILD_STATUS_SESSION
BUILD_STATUS_SESSION := $(shell python3 $(BUILD_STATUS_SCRIPT) --new-session)
endif
export BUILD_STATUS_SESSION BUILD_STATUS_SCRIPT BUILD_LOG_DIR BUILD_SPINNER V
export BUILD_STATUS_TARGET = $@
export BUILD_STATUS_SOURCE = $<
export BUILD_STATUS_OUTPUT_ROOT := $(abspath $(BUILD_DIR))
BUILD_STATUS_REAL_SHELL := $(SHELL)
BUILD_STATUS_REAL_FLAGS := $(.SHELLFLAGS)
export BUILD_STATUS_REAL_SHELL BUILD_STATUS_REAL_FLAGS
.SILENT:
override SHELL := $(BUILD_STATUS_SCRIPT)
override .SHELLFLAGS := --shell -c
MAKEFLAGS += --no-print-directory
endif
