#------------------------------------------ -*- tab-width: 8 -*-
OPENCM3_DIR	= libopencm3
DEVICE		= stm32f401cc
BINARY		= f4uac
OBJS		= main.o pwm.o usbd.o dsp.o trace.o trace_stdio.o
VPATH		= $(OPENCM3_DIR)/tests/shared

CFLAGS		+= -pipe -g -Os -flto
CFLAGS		+= -Wall -Wextra -Wshadow -Wredundant-decls

include		$(OPENCM3_DIR)/mk/genlink-config.mk
include		$(OPENCM3_DIR)/mk/gcc-config.mk

CPPFLAGS	+= -MMD
CPPFLAGS	+= -I$(OPENCM3_DIR)/tests/shared

LDFLAGS		+= --static -nostartfiles -Wl,--gc-sections
LDLIBS		+= -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group

ifneq ($(V),1)
Q := @
# Do not print "Entering directory ...".
MAKEFLAGS += --no-print-directory
endif
#---------------------------------------------------------------
OCTAVE		= octave
TABLES		= tables.h

all:		lib $(BINARY).elf $(BINARY).bin

lib:
		$(Q)$(MAKE) -C $(OPENCM3_DIR) lib TARGETS=stm32/f4 CFLAGS=-flto AR=$(CC)-ar

$(TABLES): %.h: %.m
		@printf "  OCT     $@\n"
		$(Q)$(OCTAVE) -qf $< $@

dsp.o:		$(TABLES)

include		$(OPENCM3_DIR)/mk/genlink-rules.mk
include		$(OPENCM3_DIR)/mk/gcc-rules.mk

-include *.d

.PHONY:		all lib
