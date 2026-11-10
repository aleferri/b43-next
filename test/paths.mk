# Included by the test Makefiles: the same variables as paths.sh.
REPO        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
TRACES      ?= $(REPO)/../b43-next-traces
TRACE_TOOLS ?= $(REPO)/../b43-next-trace-tools
B43_TOOLS   ?= $(REPO)/../b43-tools
