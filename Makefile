# name of your application
APPLICATION = default

BOARD ?= ec-module
EXTERNAL_BOARD_DIRS ?= $(CURDIR)/boards
EXTERNAL_MODULE_DIRS += modules

# This has to be the absolute path to the RIOT base directory:
#RIOTBASE ?= $(CURDIR)/../RIOT
RIOTBASE ?= $(CURDIR)/RIOT

DEVELHELP ?= 1

#SRCS = i2c.c main.c shell_.c

GIT_HASH = $(shell git describe --tags --first-parent --dirty --always)
CFLAGS += -DTHREAD_STACKSIZE_MAIN=\(4*THREAD_STACKSIZE_DEFAULT\) -DFW_VERSION=\"$(GIT_HASH:-dirty=\*)\"

USEMODULE += shell
# additional modules for debugging:
USEMODULE += stdio_uart ztimer ztimer_msec
FEATURES_REQUIRED += periph_gpio periph_uart periph_lpuart periph_eeprom periph_i2c

USEMODULE += ezoec ds18 
# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
