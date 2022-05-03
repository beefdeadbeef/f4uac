#---------------------------------------------------------------
CROSS	= arm-none-eabi-
CC	= $(CROSS)gcc
LD	= $(CROSS)ld
HOSTCC	= gcc
OCTAVE	= octave

#---------------------------------------------------------------
OPENCM3	?= libopencm3
VPATH	+= $(OPENCM3)/tests/shared

DEFS	= -DSTM32F4
DEFS	+= -I$(OPENCM3)/include -I$(OPENCM3)/tests/shared

CFLAGS	= -pipe -g -Os -MMD
CFLAGS	+= -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS	+= -Wall -Wextra -Wshadow -Wimplicit-function-declaration -Wredundant-decls
CFLAGS	+= -fno-common -ffunction-sections -fdata-sections
CFLAGS	+= $(DEFS)

LIBNAME	= opencm3_stm32f4
LDFLAGS	= -L$(OPENCM3)/lib
LDFLAGS	+= -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
LDFLAGS	+= --static -nostartfiles -Tstm32f4.ld -Wl,--gc-sections
LDLIBS	= -l$(LIBNAME) -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group

#---------------------------------------------------------------
BINARY	= f4uac
TABLES	= tables.h
SAMPLES = s1.s16 s2.s16 s3.s16
OBJS	= main.o pwm.o usbd.o trace.o trace_stdio.o dsp.o

all:	lib $(BINARY).elf

lib:
	make -C $(OPENCM3) TARGETS=stm32/f4 lib

%.s16:	s16.m
	@$(OCTAVE) -qf $< $* 48000

$(TABLES): %.h: %.m
	@$(OCTAVE) -qf $< $@

dsp.o:	$(TABLES) $(SAMPLES)

%.elf:	$(OBJS)
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@

dsp:	dsp.c $(TABLES) $(SAMPLES)
	$(HOSTCC) -g -Wall -O0 -DKICKSTART $< -o $@

-include *.d
