PSPSDK=$(shell psp-config --pspsdk-path)
BOOTLOADEX ?= ../../BootLoadEx
ARKSDK ?= ../../ark-dev-sdk

INCDIR = $(PSPSDK)/include $(ARKSDK)/include $(BOOTLOADEX)
TARGET = reboot.bin.gz

PYTHON=$(shell which python3)

all: $(TARGET)

CFLAGS = -std=c99 -Wall -Os -G0 -fno-pic $(addprefix -I, $(INCDIR)) -DREBOOTEX -DREDIRECT_FLASHFS

CC = psp-gcc
LD = psp-ld
STRIP = psp-strip
OBJCOPY = psp-objcopy
LINKFILE = $(BOOTLOADEX)/linkfile.l

REBOOT_OBJS = \
	main.o \
	lib.o \
	pspbtcnf.o \
	scanner.o \

C_OBJS = \
	patches.o \
	$(BOOTLOADEX)/vita_rebootex/patches.o

LIBS =
LIBDIR = 

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
LIBS += -lcolordebugger
LIBDIR += $(ARKSDK)/libs
endif

crt0.o : $(BOOTLOADEX)/crt0.S
	$(CC) -I$(PSPSDK)/include -c $< -o crt0.o

$(REBOOT_OBJS) : %.o : $(BOOTLOADEX)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

main.elf: crt0.o $(C_OBJS) $(REBOOT_OBJS)

$(TARGET): main.elf
	$(Q)$(STRIP) -s $<
	$(Q)$(OBJCOPY) -O binary $< $(patsubst %.gz,%,$(TARGET))
	$(Q)$(PYTHON) $(ARKSDK)/build-tools/gz/gz.py $(patsubst %.gz,%,$(TARGET)) $@
	$(Q)bin2c $@ payload.h rebootbuffer_vitapops
	@echo GET $@

clean:
	$(Q)rm -rf *~ *.s *.o *.elf reboot.bin reboot.bin.gz payload.h $(EXTRA_CLEAN)

include $(ARKSDK)/build-tools/beauty_bin.mak
