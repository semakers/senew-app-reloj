# Makefile of senew-app-reloj.  `make` builds build/reloj.sen
APP = reloj

# Where is the SDK?  Priority: SENEW_SDK=/path (command line or environment) > ../senew-sdk > ./.senew-sdk
# If it is nowhere, it is cloned (shallow) into ./.senew-sdk the first time.  Needs git and network once.
SENEW_SDK ?= $(firstword $(wildcard ../senew-sdk .senew-sdk))
ifeq ($(SENEW_SDK),)
  $(info senew-sdk not found next to this folder: cloning it into ./.senew-sdk ...)
  $(shell git clone --quiet --depth 1 https://github.com/semakers/senew-sdk .senew-sdk 1>&2)
  SENEW_SDK := .senew-sdk
endif
ifeq ($(wildcard $(SENEW_SDK)/mk/app.mk),)
  $(error SENEW_SDK=$(SENEW_SDK) is not a senew-sdk checkout (mk/app.mk missing))
endif

include $(SENEW_SDK)/mk/app.mk
