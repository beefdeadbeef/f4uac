#------------------------------------------ -*- tab-width: 8 -*-
OPENCM3_DIR	= libopencm3
DEVICE		= at32f403acgu
BINARY		= f4uac
OBJS		= main.o disp.o pwm.o usbd.o dsp.o tables.o

CFLAGS		+= -pipe -g -Os -flto
CFLAGS		+= -Wall -Wextra -Wshadow
CPPFLAGS	+= -MMD

include		$(OPENCM3_DIR)/mk/genlink-config.mk
include		$(OPENCM3_DIR)/mk/gcc-config.mk
include		mk/debug/config.mk
include		mk/icons/config.mk

LDFLAGS		+= --static -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
LDLIBS		+= -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group

ifneq ($(V),1)
Q := @
MAKEFLAGS += --no-print-directory
endif
#---------------------------------------------------------------
OCTAVE		= octave
TABLES		= tables.h tables.c

all:		lib $(BINARY).elf $(BINARY).bin

lib:
		$(Q)$(MAKE) -C $(OPENCM3_DIR) lib TARGETS=at32/f40x CFLAGS=-flto AR=$(CC)-ar

$(OBJS):	tables.h
disp.o:		font.h icons.h

$(TABLES):	tables.m
		@printf "  OCT     $@\n"
		$(Q)$(OCTAVE) -qf $< $@

include		$(OPENCM3_DIR)/mk/genlink-rules.mk
include		$(OPENCM3_DIR)/mk/gcc-rules.mk
include		mk/debug/rules.mk
include		mk/icons/rules.mk

-include	*.d

.PHONY:		all lib
