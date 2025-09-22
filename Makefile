TARGET = vitapops

PSPSDK = $(shell psp-config --pspsdk-path)
ARKSDK ?= ../ark-dev-sdk

C_OBJS = \
	main.o \
	src/syspatch.o \
	src/popsdisplay.o \
	src/vitaflash.o \
		
OBJS = \
	$(C_OBJS) imports.o

all: $(TARGET).prx
INCDIR = include $(ARKSDK)/include
CFLAGS = -std=c99 -Os -G0 -Wall -fno-pic

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC=1
USE_KERNEL_LIBS=1

LIBDIR = $(ARKSDK)/libs
LDFLAGS =  -nostartfiles
LIBS = -lpspsystemctrl_kernel -lpspkermit_driver -lpspkermitperipheral_driver

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
LIBS += -lcolordebugger
endif

PSP_FW_VERSION = 660

include $(PSPSDK)/lib/build.mak
